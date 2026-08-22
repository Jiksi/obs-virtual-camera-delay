# OBS Virtual Camera Delay — MVP

Experimental OBS Studio video filter that keeps a queue of GPU-rendered frames and outputs them after a configurable delay.

## Intended use

1. Create a dedicated scene, for example `Virtual Camera Output`.
2. Add the scene/source you want the virtual camera to show.
3. Add the **Virtual Camera Delay** filter to that scene/source.
4. Start with `1000-3000 ms` delay.
5. Configure OBS Virtual Camera to output that dedicated scene/source rather than Program output.

This keeps the normal Program/stream/recording path real-time while the dedicated Virtual Camera path is delayed.

## Windows build

Requirements:

- Windows x64
- PowerShell 7.2+
- Git
- CMake
- Visual Studio 2022 with Desktop development with C++

Clone the repository and run:

```powershell
pwsh -File .\scripts\build-windows.ps1
```

For a Release build:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration Release
```

The build script uses the official OBS plugin template as a disposable build workspace, downloads and verifies the required OBS dependencies, and copies the final plugin tree into:

```text
release\RelWithDebInfo\
```

The plugin DLL should be located under the generated `obs-virtual-camera-delay\bin\64bit` directory.

To create a ZIP package after building:

```powershell
pwsh -File .\scripts\package-windows.ps1
```

The package is written to `dist\`.

## Installing for a smoke test

Close OBS first. Copy the contents of the generated release package into the matching OBS Studio installation directories. Keep the plugin DLL and its `data` directory together in the layout produced by the build.

After restarting OBS, add **Virtual Camera Delay** as a video filter to a source or dedicated Virtual Camera scene. Start testing at `1000 ms` before increasing the delay.

## Current MVP limits

- Video only.
- Delay range: 0–10000 ms.
- GPU texture buffering; long delays can consume substantial VRAM.
- No adaptive memory limit yet.
- No HDR/color-space preservation layer yet.
- No automatic Virtual Camera configuration yet.
- Intended to prove the delayed-render path before optimizing storage.

## Build/CI approach

The repository keeps the plugin source small while `scripts/build-windows.ps1` bootstraps the current official `obsproject/obs-plugintemplate` build infrastructure into `.build/`. The temporary workspace is not committed.

GitHub Actions runs the same Windows build and packaging flow on pushes and pull requests to `main` and uploads the resulting ZIP as a workflow artifact.

## Next engineering milestones

1. Build and verify on Windows x64 / OBS 32.x.
2. Add VRAM estimate to properties.
3. Add hard memory/frame cap and graceful fallback.
4. Preserve OBS color spaces/HDR correctly.
5. Add buffer reset handling for source/size/FPS changes.
6. Evaluate lower-memory storage for delays above ~2–3 seconds at 1080p60.
