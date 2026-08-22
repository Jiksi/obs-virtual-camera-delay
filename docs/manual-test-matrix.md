# v0.1.0 manual test matrix

This file is the release gate. A row is `PASS` only when it was exercised in a
real OBS session; build success is not a substitute.

Test environment used on 2026-08-23:

- OBS Studio 32.2.2, Windows x64 (Windows build 26200)
- NVIDIA GeForce 930MX / D3D11
- Plugin `0.1.0` RelWithDebInfo

| Test | Result | Evidence / note |
|---|---|---|
| Plugin load/startup | PASS | OBS normal launch; startup crash caused by an early frontend lookup was reproduced, fixed, and regression-tested. |
| 720p60, 1000 ms | PASS | Feeder logged 1280x720, 1000 ms, expected 80.4 MiB. |
| 720p60, 500 ms | PASS | Setting persisted in profile and next start logged 500 ms / 40.9 MiB. |
| 720p30: 0/500/1000/3000 ms | PENDING | Requires matrix run. |
| 720p60, 3000 ms | PASS | Feeder logged 1280x720, 3000 ms, expected 238.6 MiB. |
| 720p60, 0 ms | PENDING | Requires matrix run. |
| 1080p30, 3000 ms | PASS | Feeder logged 1920x1080, 3000 ms, expected 269.9 MiB; 3,958 frames in the first run and 0 dropped. |
| 1080p30: 0/500/1000 ms | PENDING | Requires matrix run. |
| 1080p60, 3000 ms | PASS | Feeder logged 1920x1080, 3000 ms, expected 536.9 MiB. |
| 1080p60: 0/500/1000 ms | PENDING | Requires matrix run. |
| Repeated Start/Stop | PASS (5 cycles) | All cycles rewired/restored at 60 FPS. Logged drop count was zero in every cycle. |
| Scene/source changes while active | PASS | Hid and restored the active Video Capture Device while delayed Virtual Camera remained active at 1080p30; preview switched to black and recovered without a crash or restart. |
| Recording while delayed VC active | PASS | Local recording ran while Virtual Camera stayed active at 60/60 FPS. |
| Streaming while delayed VC active | PENDING | Requires an authorized stream destination. |
| Zoom consumer | PENDING | Zoom is not installed in the test environment. |
| Second consumer (Meet/Discord/Teams) | PASS (Google Meet) | Meet selected the normal `OBS Virtual Camera` device and reported `Pratinjau video AKTIF` / `Kamera Anda aktif`; no duplicate virtual-camera device was present. |
| 30 minutes at 1080p60 | PASS | 1920x1080, 60 FPS, 3000 ms ran for 30 minutes. Working set stabilized around 739-742 MiB (741.7 MiB final), 113,054 frames received, 112,873 emitted, 0 dropped, and 40 renderer-lag frames (0.0%). Stopping reclaimed working set to 129.8 MiB. |
| Stop/shutdown cleanup | PASS | Active shutdown stopped 25,911-frame run, released the feeder, logged unload, and exited without a new crash report. |
| HDR PQ/HLG rejection | CODE/UNIT ONLY | Runtime HDR configuration still needs a manual rejection test. |

## Compatibility evidence for implementation issues

- Normal OBS transport: Google Meet acquired `OBS Virtual Camera` while the
  delayed feeder was active; no second driver/device was installed.
- Start/stop recovery: five 720p60 cycles plus the 1080p30/1080p60 runs stopped
  cleanly with zero feeder drops and no stale process state.
- Geometry/FPS recovery: the camera restarted successfully after changing from
  720p60 to 1080p60 and 1080p30, then returned to 720p60.
- Source recovery: hiding and restoring the live source while the camera was
  active produced black/live program transitions without a crash.
- The user's original OBS configuration was restored after testing: canvas
  1366x768, output 1280x720, 60 FPS, delay 500 ms, Virtual Camera active.

## Timing method

For consumer validation, display a millisecond clock or frame counter in Program,
capture Program and the consumer preview in the same screen recording, and
compare matching frames. Accept a difference within two video-frame intervals
of the configured delay after warm-up.

## Long-run memory method

At 1080p60/3000 ms, sample the OBS process working set before start, after queue
warm-up, and every five minutes for 30 minutes. The queue may rise to its stable
working set (~534 MiB payload), but samples after warm-up must not show a
continuing upward trend. Record all samples in this file before release.
