# OBS Virtual Camera Delay

Experimental OBS Studio plugin that automatically delays the built-in Virtual Camera while leaving streaming and recording real-time.

```text
OBS Program ──> Stream / Recording (real-time)
      |
      └──> GPU delay buffer ──> OBS Virtual Camera (delayed)
```

The plugin is frontend/output-oriented. It does **not** need to be added as a filter to a source or scene.

## Supported workflow

This version targets OBS Studio 32.x on Windows x64 and currently supports the built-in Virtual Camera when its output type is **Program**.

1. Open the Virtual Camera configuration and select **Program** as its output type.
2. Open **Tools → Virtual Camera Delay Settings**.
3. Start with a delay of `1000-3000 ms`.
4. Click OBS's normal **Start Virtual Camera** button.
5. Open the OBS Virtual Camera in Zoom, Meet, Discord, or another consumer.

The plugin receives the built-in Virtual Camera lifecycle events and automatically attaches a private delayed Program view. Stopping Virtual Camera releases the view and its GPU frame buffer.

## Important startup behavior

OBS does not expose a public callback before the built-in Virtual Camera connects to its video media. The first `VIRTUALCAM_STARTED` event arrives after capture has already started.

To stay on public OBS interfaces, this plugin performs one short internal stop/restart when Virtual Camera starts, then reconnects that output to the delayed view. The OBS Virtual Camera control can briefly change state during this handoff. See [Virtual Camera integration](docs/virtual-camera-integration.md) for the design and constraints.

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

The build script uses the official OBS plugin template as a disposable build workspace, downloads and verifies the OBS 32.2.2 dependencies, and copies the final plugin tree into:

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

After restarting OBS:

1. Confirm the OBS log contains `[obs-virtual-camera-delay] loaded`.
2. Set Virtual Camera output type to **Program**.
3. Put a visible clock or moving object in Program.
4. Set **Tools → Virtual Camera Delay Settings → Delay** to `1000 ms`.
5. Start streaming or recording if you want to compare the real-time path.
6. Click **Start Virtual Camera** and observe it in a separate consumer.
7. Confirm Virtual Camera is about one second behind while Program, streaming, and recording remain real-time.
8. Stop and start Virtual Camera at least five times and check the OBS log for errors.
9. Repeat at `2000 ms` and `3000 ms` while monitoring VRAM.

If Virtual Camera is configured for Preview, Scene, or Source output, the plugin logs a warning and leaves the Virtual Camera unchanged instead of overriding the user's selection.

## Current limits

- Video only; audio is not delayed.
- Virtual Camera output type must be **Program**.
- Delay range: 0–10000 ms.
- A brief internal Virtual Camera restart is required at startup.
- GPU texture buffering can consume substantial VRAM for long delays.
- No adaptive memory limit yet.
- No HDR/color-space preservation layer yet.
- Runtime behavior must still be smoke-tested inside OBS on a Windows host; CI/build validation alone cannot exercise a real Virtual Camera consumer.

## Build/CI approach

The repository keeps the plugin source small while `scripts/build-windows.ps1` bootstraps the current official `obsproject/obs-plugintemplate` build infrastructure into `.build/`. The temporary workspace is not committed.

GitHub Actions runs the same Windows build and packaging flow on pushes and pull requests to `main` and uploads the resulting ZIP as a workflow artifact.

## Next engineering milestones

1. Complete the Windows/OBS runtime smoke-test matrix.
2. Add a VRAM estimate and hard memory/frame cap.
3. Preserve OBS color spaces/HDR correctly.
4. Evaluate lower-memory storage for delays above ~2–3 seconds at 1080p60.
5. Revisit Preview/Scene/Source modes if OBS exposes a public pre-start or view-selection seam.
