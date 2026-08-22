# Virtual Camera integration

## Product seam

The delay belongs only on the Virtual Camera branch:

```text
                              +--> stream output
OBS video / selected VC view -+--> recording output
                              |
                              +--> delayed feeder output
                                      |
                                      v
                              TimestampFrameBuffer (owned NV12)
                                      |
                                      v
                              private async OBS source
                                      |
                                      v
                              private OBS video view
                                      |
                                      v
                              built-in virtualcam_output
                                      |
                                      v
                              OBS shared-memory queue/driver
```

The plugin does not create a shared-memory mapping or register a DirectShow
device. It reuses OBS's `virtualcam_output`, so consumers still see exactly the
normal **OBS Virtual Camera** device and OBS remains the owner of its Windows
transport.

## Components

- `TimestampFrameBuffer` is OBS-independent. It owns tightly packed NV12 frames,
  uses capture timestamps rather than a fixed frame count, preserves order, and
  enforces the byte limit.
- `delayed_virtual_camera_output` is a private raw-video feeder. OBS converts the
  selected Virtual Camera media to NV12 before its callback. A due frame is
  emitted with the current delivery timestamp so libobs schedules old content
  now rather than treating it as stale.
- `virtual_camera_delay_private_source` is an async source used only inside a
  private view. It also supplies the settings dialog; it is never exposed as a
  scene/filter workflow.
- `VirtualCameraDelayController` follows frontend start/stop/exit events and
  performs the rewire only after the frontend reports that it finished loading.

## Lifecycle

```text
Idle
  | user starts built-in VC
  v
Built-in VC starts -> controller saves original media -> requests stop
  | deactivate signal
  v
Create private view -> start feeder on original media -> restart built-in VC
  |
  v
ActiveDelayed
  | user stops VC / OBS exits
  v
Stop feeder -> clear async frames -> free NV12 queue -> restore original media
  |
  v
Idle
```

Calling `obs_frontend_get_virtualcam_output()` during module load is unsafe
because the frontend implementation is not constructed yet. Attachment is
therefore deferred to `OBS_FRONTEND_EVENT_FINISHED_LOADING`.

## Geometry and color safety

The feeder snapshots width, height, FPS, color space, and range on every start.
OBS settings that change geometry require Virtual Camera restart; the old feeder
is destroyed and its queue flushed before the new configuration is built. Scene,
program, and source content changes do not change frame geometry and flow through
the original media automatically.

The conversion is explicitly NV12. SDR matrices/ranges are copied into the async
source frame. PQ and HLG are rejected and the controller restores a real-time
Virtual Camera rather than sending incorrectly interpreted frames.

## Concurrency and ownership

The raw-video callback owns all copied payloads. The queue is used only by that
callback, so it needs no hot-path mutex. A short mutex protects acquisition of a
reference to the private target source; the actual libobs frame copy happens
after the mutex is released. Stop first disables capture, then clears the queue
and target reference.
