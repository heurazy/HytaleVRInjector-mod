# Building Hytale VR

## Requirements

- Windows 10/11
- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.20 or newer

## Build

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --install build --config Release --prefix dist
```

The final rebuildable package is written to `dist`.

## Runtime files

The dashboard build copies these files next to `hytale_camera_dashboard.exe`:

- `hytale_vr_camera_hook_v120_native_hand.dll`
- `HytaleUIScaleHook.dll`
- `openvr_api.dll`
- `hytalevr.vrmanifest`
- `hytalevr_actions.json`
- `hytalevr_bindings_oculus_touch.json`

## Debug logging

Release builds keep noisy logs disabled by default. To enable debug logs for local diagnosis:

```powershell
$env:HYTALEVR_DEBUG_LOGS = "1"
```

Logs are written under the current Windows temp directory in `HytaleVR`.
