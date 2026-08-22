# OBS Virtual Camera Delay

Experimental OBS Studio plugin exploring a dedicated delayed Virtual Camera
output while leaving streaming and recording real-time.

```text
OBS Program ──> Stream / Recording (real-time)
      |
      └──> custom raw-video output ──> CPU delay queue ──> future OS sink
```

The plugin is frontend/output-oriented. It does **not** register a source or
scene filter as its user-facing feature.

## Current proof of concept

This version targets OBS Studio 32.x on Windows x64. At module load it registers
`delayed_virtual_camera_output`. Once the OBS frontend finishes loading, plugin
code creates the output, connects it directly to Program video, and starts raw
video capture.

Incoming NV12 frames are copied into a timestamp-based delay queue. Frames that
reach the configured delay boundary are counted and released from the queue. The
first delayed frame produces an OBS log entry, providing a runtime smoke-test
point for the new output path.

This milestone intentionally stops at the delayed-output boundary: it does not
yet forward delayed frames to the Windows Virtual Camera shared-memory sink.
That platform adapter is the next architectural layer. See
[Virtual Camera integration](docs/virtual-camera-integration.md).

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

1. Confirm the log contains `custom output started` and `loaded`.
2. Keep Program video active for longer than three seconds.
3. Confirm the log contains `first delayed video frame reached the custom output boundary`.
4. Start streaming or recording and confirm it remains functional; the custom
   output is an additional raw-video consumer and does not rewire either path.
5. Exit OBS and confirm the custom output reports its received-frame count.

## Current limits

- Video only; audio is not captured or delayed.
- The PoC queue delay defaults to 3000 ms and supports 0–10000 ms internally.
- The delay is not yet exposed through a settings UI.
- Delayed frames are not yet sent to the Windows Virtual Camera driver.
- CPU frame copies can consume substantial RAM; there is no adaptive cap yet.
- Runtime behavior must still be smoke-tested inside OBS; build validation cannot
  exercise raw-frame delivery.

## Build/CI approach

The repository keeps the plugin source small while `scripts/build-windows.ps1` bootstraps the current official `obsproject/obs-plugintemplate` build infrastructure into `.build/`. The temporary workspace is not committed.

GitHub Actions runs the same Windows build and packaging flow on pushes and pull requests to `main` and uploads the resulting ZIP as a workflow artifact.

## Next engineering milestones

1. Add the Windows Virtual Camera sink behind the delayed-frame boundary.
2. Expose delay configuration without introducing a source/filter workflow.
3. Add a RAM estimate and hard memory/frame cap.
4. Preserve OBS color spaces/HDR correctly.
5. Complete the Windows/OBS runtime smoke-test matrix.
