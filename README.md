# OBS Virtual Camera Delay

Windows x64 plugin that delays the built-in **OBS Virtual Camera** while
streaming and recording stay real-time. No source filter and no second virtual
camera driver are installed.

```text
                         +--> Stream / Recording (real-time)
OBS selected VC media ---+
                         +--> NV12 feeder --> timestamp queue --> private view
                                                               |
                                                               v
                                                    OBS Virtual Camera output
                                                               |
                                                               v
                                                   normal OBS Virtual Camera
```

## Supported v0.1.0 configuration

- OBS Studio 32.x on Windows x64.
- SDR video (Rec. 601, Rec. 709, or sRGB) converted to NV12.
- Delay from 0 through 3000 ms; default 1000 ms.
- Hard queue limit of 768 MiB. A resolution/FPS/delay combination that would
  exceed it is clamped and reported in the OBS log.
- Video only. Virtual Camera audio behavior remains controlled by OBS.

HDR PQ/HLG is rejected for v0.1.0. Settings changes take effect on the next
Virtual Camera start, not during an active run.

## Install

1. Close OBS Studio.
2. Download `obs-virtual-camera-delay-v0.1.0-windows-x64.zip` and extract it.
3. Copy the extracted `obs-virtual-camera-delay` directory into one of:
   - current user: `%APPDATA%\obs-studio\plugins\`
   - all users (administrator required): `%PROGRAMDATA%\obs-studio\plugins\`
4. Start OBS and confirm **Tools > Virtual Camera Delay Settings** exists.

The final path should contain:

```text
obs-studio/plugins/obs-virtual-camera-delay/
|-- bin/64bit/obs-virtual-camera-delay.dll
`-- data/locale/{en-US,id-ID}.ini
```

Do not install the plugin in both user and all-user locations. OBS gives the
all-user bundle precedence, which can make an older copy appear to remain
installed.

To uninstall, close OBS and delete only the `obs-virtual-camera-delay` bundle
directory from the location used above. The plugin does not install or remove
the OBS Virtual Camera driver.

## Use

1. Open **Tools > Virtual Camera Delay Settings**.
2. Enable delayed mode and choose 0–3000 ms.
3. Click **Start Virtual Camera** in OBS.
4. Select the existing **OBS Virtual Camera** device in the consuming app.

The plugin briefly restarts the output internally after the initial click so it
can insert the delayed private view. The OBS button remains in the active state.
Stopping Virtual Camera flushes all queued RAM and restores the original media.

The OBS log records the actual delay, resolution, expected RAM, clamping,
unsupported color spaces, emitted/drop counts, and cleanup.

## Build and test

Requirements:

- Windows x64 and PowerShell 7.2+
- Git and CMake 3.28+
- Visual Studio 2022 with **Desktop development with C++**

From a clean checkout:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration RelWithDebInfo
pwsh -File .\scripts\package-windows.ps1 -Configuration RelWithDebInfo
```

The build script downloads hash-verified OBS 32.2.2 dependencies, runs the
deterministic core tests, builds the DLL, and stages the install bundle under
`release\RelWithDebInfo`. Packaging validates the DLL/locale layout and writes
the versioned ZIP to `dist\`.

Core tests can also be run independently:

```powershell
cmake -S tests -B build-tests
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

CI runs both the portable core-test target and the full Windows build/package
flow. See [the manual test matrix](docs/manual-test-matrix.md) for the release
gate and [the integration design](docs/virtual-camera-integration.md) for the
contributor architecture.

## Memory trade-off

NV12 uses approximately `width * height * 1.5` bytes per frame. Expected queue
memory is approximately `width * height * 1.5 * FPS * delay_seconds`.

| Mode | 1000 ms | 3000 ms |
|---|---:|---:|
| 720p30 | ~40 MiB | ~119 MiB |
| 720p60 | ~79 MiB | ~238 MiB |
| 1080p30 | ~89 MiB | ~267 MiB |
| 1080p60 | ~178 MiB | ~534 MiB |

## Troubleshooting

- **Tools menu item is missing:** inspect the OBS log for a module load error and
  verify the bundle layout above.
- **Old UI still appears:** remove a duplicate bundle from either `%APPDATA%` or
  `%PROGRAMDATA%`; then restart OBS.
- **Virtual Camera falls back to real-time:** inspect the log. HDR or an invalid
  video configuration causes a safe fallback instead of a crash.
- **Configured delay was reduced:** the requested combination exceeded the
  768 MiB queue limit.
- **Consumer is black:** wait at least the configured delay, verify delayed mode
  is enabled, and confirm the app selected **OBS Virtual Camera**.

## Known limitations

- Windows x64 only; OBS 32.x is the validated ABI target.
- SDR NV12 video only; no HDR PQ/HLG support.
- Maximum configured delay is 3000 ms and may be clamped by memory safety.
- Settings apply on the next Virtual Camera start.
- The queue makes one CPU copy per input frame and one libobs async-source copy
  per emitted frame.
- Consumer-app and long-duration results are tracked explicitly in
  [the manual matrix](docs/manual-test-matrix.md); release claims must not exceed
  its completed rows.
