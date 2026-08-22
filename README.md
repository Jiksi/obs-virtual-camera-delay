# OBS Virtual Camera Delay — MVP

Experimental OBS Studio video filter that keeps a queue of GPU-rendered frames and outputs them after a configurable delay.

## Intended use

1. Create a dedicated scene, for example `Virtual Camera Output`.
2. Add the scene/source you want the virtual camera to show.
3. Add the **Virtual Camera Delay** filter to that scene/source.
4. Start with `1000-3000 ms` delay.
5. Configure OBS Virtual Camera to output that dedicated scene/source rather than Program output.

This keeps the normal Program/stream/recording path real-time while the dedicated Virtual Camera path is delayed.

## Current MVP limits

- Video only.
- Delay range: 0–10000 ms.
- GPU texture buffering; long delays can consume substantial VRAM.
- No adaptive memory limit yet.
- No HDR/color-space preservation layer yet.
- No automatic Virtual Camera configuration yet.
- Intended to prove the delayed-render path before optimizing storage.

## Development approach

Use this source inside the official `obsproject/obs-plugintemplate` bootstrap. The official template supplies the Windows build scripts, dependency bootstrap, packaging, and CI files.

For an existing template project, copy:

- `src/plugin-main.c`
- `src/virtual-camera-delay.c`
- `src/virtual-camera-delay.h`
- locale files from `data/locale/`

Then add `src/virtual-camera-delay.c` and the header to the template's `target_sources(...)`.

## Next engineering milestones

1. Build and verify on Windows x64 / OBS 32.x.
2. Add VRAM estimate to properties.
3. Add hard memory/frame cap and graceful fallback.
4. Preserve OBS color spaces/HDR correctly.
5. Add buffer reset handling for source/size/FPS changes.
6. Evaluate lower-memory storage for delays above ~2–3 seconds at 1080p60.
