# HytaleVRInjector-mod

Experimental Windows x64 VR injector/dashboard for Hytale.

This repository contains the native dashboard, the injected VR camera hook, the UI scaling hook, SteamVR action manifests, tests, and the minimal third-party sources/binaries required to rebuild the project.

## Status

This is an experimental mod. It is not affiliated with or endorsed by Hytale, Hypixel Studios, Valve, OpenVR, or MinHook.

## Repository Layout

- `src/` - dashboard, VR hook, probes, shared headers, and helper tools
- `src/ui_scale_hook/` - OpenGL UI scaling hook built as `HytaleUIScaleHook.dll`
- `tests/` - native math/unit tests
- `third_party/openvr/` - minimal OpenVR headers/import library/runtime DLL
- `third_party/minhook/` - vendored MinHook sources used by the UI hook
- `BUILDING.md` - rebuild instructions

## How to Use

1. Start SteamVR and make sure your headset is connected.
2. Launch Hytale and enter a world or join a server.
3. Disable `FXAA` in the Hytale graphics settings.
4. Press `F7` in-game to show the player coordinate block.
5. Start `hytale_camera_dashboard.exe`.
6. In the dashboard, click `Scan player block`.
7. Select the detected coordinate block from the list.
8. Click `Center VR` to inject and align the VR view.

Keep SteamVR running while using the mod.
tutorial video : https://youtu.be/ktmVUCQHKF0
## Build

Requirements:

- Windows 10/11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20 or newer

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --install build --config Release --prefix dist
```

The runnable package is generated in `dist`.

## Debug Logs

Release builds keep verbose logs disabled by default.

```powershell
$env:HYTALEVR_DEBUG_LOGS = "1"
```

Logs are written under `%TEMP%\HytaleVR`.

## License

Project code is licensed under Apache-2.0. Third-party dependencies keep their own licenses in `third_party/`.
