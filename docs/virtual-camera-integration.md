# Virtual Camera integration

## Requirement

The product seam is the built-in OBS Virtual Camera control, not a source or scene filter:

```text
Program ──> Stream / Recording
    |
    └──> delayed private video view ──> built-in Virtual Camera output
```

Only the Virtual Camera branch may be delayed.

## OBS 32.2.2 public-interface constraint

OBS configures the built-in Virtual Camera in `BasicOutputHandler::StartVirtualCam()` by calling `obs_output_set_media()` immediately before `obs_output_start()`.

The raw output connects to that selected `video_t` while `obs_output_start()` is running. The output's `starting` signal and `OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED` are emitted only after that work. OBS 32.2.2 does not expose a public pre-start Virtual Camera callback or a public interface for inserting a video transform into an already-connected raw output.

Consequences:

- Setting media at plugin load is overwritten by the frontend immediately before start.
- Changing `obs_output_set_media()` after the started event does not move the already-connected raw-video callback.
- Attaching a filter to Program would also delay streaming and recording, violating the requirement.
- Accessing private `obs_output` or frontend implementation fields would tightly couple the plugin to OBS internals and ABI details.

## Selected architecture

`VirtualCameraDelayController` is the deep module at the frontend/output seam. Its external interface is only load/unload; event ordering, output signals, the internal restart, media rewiring, and cleanup stay in its implementation.

For Program mode it performs this lifecycle:

1. Observe `OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED`.
2. Verify that `obs_output_video(virtualCam) == obs_get_video()` so the selected mode is Program.
3. Stop the Virtual Camera once.
4. Wait for the output's `deactivate` signal, which confirms the original raw-video callback has disconnected.
5. Create a private `obs_view_t` containing `virtual_camera_delay_program_source`.
6. Attach the view's `video_t` to only the built-in Virtual Camera output.
7. Start that output directly.
8. On the user's normal stop, restore Program media and destroy the private view.

The private source re-renders the public OBS output channels into a queue of GPU textures. Resolution or FPS changes rebuild the queue; source deactivation releases it.

## Failure behavior

The controller does not silently change unsupported Virtual Camera selections. Preview, Scene, and Source modes remain pass-through and produce an OBS log warning. If the private view or restart fails, the plugin logs an error, restores Program media while the output is inactive, and releases the partial view.

## Rejected alternatives

- **Effect Filter on every source/scene:** wrong user experience and easy to configure incorrectly.
- **Filter on Program:** delays streaming and recording too.
- **Replace OBS's `virtualcam_output` type:** conflicts with the platform implementation and duplicates OS-specific output code.
- **Patch private OBS structures or disconnect its private callback:** ABI-fragile and not acceptable before the public-interface approach is runtime-tested.
- **Change output media after `VIRTUALCAM_STARTED` without restarting:** ineffective because raw capture is already connected to the previous `video_t`.

## Verification boundary

The Windows build verifies symbols, C++ compilation, frontend linkage, packaging layout, and locale resources. A real OBS process plus a Virtual Camera consumer is still required to verify timing, UI handoff behavior, repeated start/stop cycles, and resource usage.
