# Virtual Camera integration

## Requirement

The product seam is an OBS output, not a source or scene filter:

```text
Program ──> Stream / Recording
    |
    └──> delayed_virtual_camera_output ──> Windows Virtual Camera sink
```

Only the Virtual Camera branch may be delayed.

## Proof-of-concept architecture

The plugin registers a raw-video `obs_output_info` with the ID
`delayed_virtual_camera_output`. After `OBS_FRONTEND_EVENT_FINISHED_LOADING`,
`VirtualCameraDelayController` creates that output, assigns `obs_get_video()` as
its video media, and starts it from plugin code.

Starting the output calls `obs_output_begin_data_capture()`. OBS then supplies
NV12 Program frames through the output's `raw_video` callback. Each callback
copies the frame into an owned CPU buffer. Once the source timestamp has advanced
by the configured delay, the oldest frame crosses the delayed-output boundary and
is removed from the queue.

This is an additional raw-video consumer. It does not change Program media or the
video media used by streaming and recording outputs.

## Transition from the legacy filter path

`virtual-camera-delay.cpp` remains in the build as legacy/reference code during
the transition, but `obs_module_load()` no longer registers its source type. It
therefore does not appear as the plugin's primary user-facing feature and cannot
be added accidentally to a scene or source.

The old frontend logic that stopped, rewired, and restarted OBS's built-in
Virtual Camera has also been removed. The controller now owns only the lifecycle
of the plugin's custom output.

## Current boundary

The PoC proves these parts of the architecture:

- the custom output type registers successfully;
- plugin code creates and starts an instance;
- the output receives Program video frames;
- timestamped frames remain queued until the delay is reached;
- streaming and recording remain on their existing, real-time output paths; and
- plugin unload stops and releases the output.

It does not yet implement the final Windows Virtual Camera sink. The delayed
frame that crosses the output boundary is currently counted and released. A
future platform adapter will write that frame to the Windows shared-memory queue
used by a Virtual Camera consumer.

## Runtime verification

A Windows OBS smoke test should confirm the following log sequence:

1. `custom output started ... with 3000 ms delay`
2. `custom delayed virtual camera output created and started independently ...`
3. `first delayed video frame reached the custom output boundary`
4. on shutdown, `custom output stopped after receiving ... frames`

Build and packaging validation prove API compatibility and artifact layout, but
only a running OBS video pipeline can exercise the raw-frame callback.
