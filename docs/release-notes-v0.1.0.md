# OBS Virtual Camera Delay v0.1.0

Initial Windows x64 release for OBS Studio 32.x.

## Highlights

- Delays the normal OBS Virtual Camera without installing another device.
- Timestamp-based owned NV12 queue with 0–3000 ms configuration.
- Tools-menu settings, per-profile persistence, and clear active/inactive text.
- 768 MiB hard memory bound with safe clamping and diagnostic logs.
- Automatic start/stop, queue cleanup, geometry rebuild, and SDR color metadata.
- Streaming and recording keep their original real-time media paths.

## Known limitations

- Windows x64 and OBS 32.x only.
- SDR video only; HDR PQ/HLG is rejected.
- Video delay only; settings apply on the next Virtual Camera start.
- Long delays use substantial RAM (about 534 MiB at 1080p60/3000 ms).

Install and uninstall instructions are in the repository README.
