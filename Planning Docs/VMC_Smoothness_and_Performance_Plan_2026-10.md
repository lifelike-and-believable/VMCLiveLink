# VMC smoothness and performance plan

October 2026. With XR Animator streaming to a VRM avatar, the motion looks choppy although the VMC source reports receiving well over 30 fps. This plan says why, what to measure, and what to change, as pull requests with acceptance criteria. Engine references are to UE 5.6; 5.7 and 5.8 have the same code unless noted.

## 1. What we know

### Measured

| What | Value | Where from |
|---|---|---|
| XR Animator's frame rate at the source | 15 to 27 fps (lower while the machine was building) | the source's status line and `VMC.Stats`, 2026-10-07 |
| Jitter (mean deviation of the frame interval) | about 31 ms at 27 fps: about one frame interval | the same |
| Packets per frame | about 4 (bones, blend shapes, apply, OK): 55 bones and about 16 curves | `VMC.Stats` |
| Building a VMC frame (55 bones, 60 curves) | 17 µs for the messages, 1 µs for the frame | `VMC.Perf.StreamFrame` |
| Spring solver, 200 joints and 30 colliders | 0.13 ms per frame | `VRM.Perf.SpringSolver` |

The plugins' own CPU cost is far below a frame's budget. The choppiness is a timing problem: what Live Link gives the avatar between poses that arrive irregularly.

### Baseline (O1), 2026-10-08

XR Animator on the same machine as the editor (loopback, receive thread), interpolation on (O2):

| Condition | Frames/s | Interval p50 / p95 / max | Held newest frame | Read time back | Smooth offset |
|---|---|---|---|---|---|
| Editor closed (a 30 s recording, by `vmc_sender.py record`) | 60.0 | 15 / 23 / 111 ms | (no editor) | | |
| Editor focused, CPU at 100% | 44 to 50 | 0.4 to 14 / 85 to 90 / 271 to 303 ms | 27 to 37% | 6 to 8 per 12 s | 0.2 to 88 ms, up to 54 ms in one tick |
| XR Animator focused, editor beside it | about 25 | 0.5 / 201 / 472 ms | 36% | 10 per 13 s | 0.2 to 212 ms |
| Steady 60 fps synthetic sender, same load | 60 | 13 to 15 / 48 to 49 / 89 to 133 ms | 11% | 0 | 1.4 to 46 ms |
| XR Animator, editor capped at 30 fps, XR Animator not rendering | 53 | 12 / 65 / 145 ms | 18% | 0 | 0.1 to 62 ms |

- **The cause is CPU load, not the network or the receive path.** All 8 logical cores were busy (the editor about 2.5 to 2.9 cores, XR Animator's tracking 1.7 to 2.6). The synthetic sender's own send times were already p95 46 ms late; the editor received them p95 49 ms apart, so the receive path adds almost nothing. A starved XR Animator sends in bursts (half the intervals under a millisecond) with gaps of hundreds of milliseconds.
- **Background throttling of the sender was ruled out:** focusing XR Animator made it worse, not better.
- **Live Link's smooth offset follows the bursts** (finding 2, now measured), so its read time lurches and steps back.
- What helps on the user's side: capping the editor's frame rate (`t.MaxFPS`) and lightening XR Animator (no rendering, lower camera resolution). Recorded for the User Guide (O6/O7).

### How Live Link times a VMC subject (engine source)

- Frames are stamped with their arrival time on the receive thread (`VMCUdpReceiver.cpp`), or when the game thread handles them with **Receive Thread** off.
- Each engine tick, Live Link builds the subject's snapshot (what the Live Link Pose node reads) at `ReadTime = now - EngineTimeOffset - EngineTimeClockOffset - SmoothEngineTimeOffset` (`LiveLinkSubject.cpp:191`):
  - `EngineTimeOffset`: the source's setting, 0 by default.
  - `EngineTimeClockOffset`: an estimate of the gap between the frames' stamps and when the client processes them (up to one engine tick here).
  - `SmoothEngineTimeOffset`: the average arrival interval times `LiveLink.TimedDataInput.NumFramesForSmoothOffset` (a console variable, 1.5 by default, global to every Live Link source): about 55 ms at 27 fps (`LiveLinkTimedDataInput.cpp:260`).
- So Live Link already evaluates about one and a half frames behind the newest, which gives an interpolator two frames to blend between, most of the time.

### Found in the code

1. **New VMC subjects have no interpolation: the main cause.** When Live Link creates a subject from pushed static data, it gives it the role's default interpolation processor, and the role's settings class and pre-processors (`LiveLinkClient.cpp`, `PushSubjectStaticData_Internal`). Our source creates its subject itself, in `FVMCLiveLinkSource::EnsureSubjectSettingsWithDefaults`, with a fresh `ULiveLinkSubjectSettings` and none of these. The Live Link panel shows **Interpolation: None** for `VMC_Subject`. Without a processor, the snapshot is the frame closest to the read time, so the avatar holds each pose until the next one: steps at the sender's rate, with its jitter.
2. **The smooth offset itself jitters with this stream.** `UpdateSmoothEngineTimeOffset` treats an interval more than 5 ms from the average as a change of rate and, after 5 such intervals, restarts its average (`FrameIntervalThreshold`, `FrameIntervalSnapCount`). With about 31 ms of jitter, nearly every interval counts. A model of the algorithm (Gaussian intervals, 37 ms mean, 39 ms deviation; not a capture) gives an offset between 17 and 123 ms that changes by 6 ms at the median, 17 ms at p90 and 43 ms at most per arriving frame. A change larger than an engine tick moves the read time backwards, so the avatar can step back and forth even with interpolation. This needs measuring on the real stream (O1).
3. **The editor in the background stops its viewports.** With XR Animator focused, the editor is a background window. With **Editor Preferences → General → Performance → Use Less CPU when in Background** (on by default), it slows its tick and turns realtime off for its viewports (`EditorEngine.cpp`), so the viewport effectively stops updating, whatever the data. This is the user's setting; the plugin doesn't change it.
4. **Republishing static data clears the buffer.** Every static data push (a new curve or bone name, a remapper or setting change) clears the subject's frames (`PushSubjectStaticData_Internal` → `ClearFrames`), so the avatar holds for about the evaluation delay. Rare during a stream, but visible.
5. **Spring bones step at render rates that aren't a multiple of 60 Hz.** `FVRMSpringSolver::Step` takes fixed 60 Hz steps and outputs the latest step's tails without blending in the time left over, so at 75 or 144 Hz, or a varying 45 to 55 fps, frames with no step keep the tails still while the body moves.

### Not known yet

- ~~How large the jitter is on the real stream~~ and ~~how the smooth offset behaves on it~~: measured, see the baseline above.
- Whether, in UE 5.6, the animation role's default interpolation processor resolves: 5.6's `BaseGame.ini` names the role as `/Script/LiveLink.LiveLinkAnimationRole`, though the class is in `LiveLinkInterface` (5.7 and 5.8 name it there). If it doesn't resolve, the project-wide default `LiveLinkBasicFrameInterpolationProcessor` would apply, which blends curves but not bone transforms.

## 2. Decisions for the owner

| ID | Decision | Recommendation |
|---|---|---|
| S-1 | How to keep the read time behind the newest frame: Live Link's smooth offset (automatic, but it jitters with this stream: finding 2), or a fixed delay | **Decided 2026-10-08, revised the same day on real data: O3b, opt-in.** O3a (steady frame times, #203) was tried first: on a 30 s recording of XR Animator under load it more than doubled held reads (14% to 35%), because late frames delivered in bursts get times in the past and Live Link skips them. The project setting **Fixed Live Link Delay (All Sources)** (off by default) sets `LiveLink.TimedDataInput.NumFramesForSmoothOffset` to 0, with the VMC source's Engine Time Offset at 0.05 to 0.075 s (modelled at 0.05 and 0.066 s: no lurch or step back on any trace, held 17% / 11%, latency about 66 / 82 ms). Steady frame times stay, off by default, for steady senders. |
| S-2 | Change the user's editor settings (background throttling) | No. Document it; consider a hint in the source's status line (O6). |

## 3. Work

In order. Each item is one pull request following CONTRIBUTING (review, CI on three engines, CHANGELOG, docs).

### O1. Measure

Make smoothness a number before changing it.

- **Arrival:** `VMC.Stats` gains the frame interval's percentiles (p50, p95, max) next to its mean and jitter, and says which receive path is in use (thread, or game thread, whose stamps are quantised to engine ticks).
- **Evaluation:** the subject's read-time offsets (user, clock, smooth) and whether each read held the newest frame, counted by `VMC.Stats` itself after every Live Link tick (`ILiveLinkClient::OnLiveLinkTicked`). The offsets come from the source's `BufferSettings`, which Live Link updates. The newest frame's time comes from `GetSubjectFrameTimes`, which already includes the clock and smooth offsets. A read counts as holding the newest frame when the read time is past it. On the interpolated path, that is when the pose repeats. On the closest-frame path, any read past the second-newest frame shows the newest, so poses repeat more often than it counts. (Live Link's own `GetLastEvaluationData` and its overflow and underflow counts are updated only with buffer statistics on, and mean different things on the two paths, so they aren't used. No `TimeManagement` dependency.) `LogLiveLink Verbose` also prints the read time and offsets.
- **A recording to replay:** fix `scripts/vmc_sender.py record` first: it stamps a packet with the time it *started waiting* for it (`now` is taken before `recvfrom`), so a replay shifts every frame one interval early; and `replay` sleeps with `time.sleep`, whose resolution on Windows before Python 3.11 is about 15.6 ms. Take the time after `recvfrom`, and require Python 3.11 or spin for the last millisecond. Then record XR Animator and check that `VMC.Stats` reads the same for the live stream and its replay. Keep the recording out of the repository (decision D-8).

Acceptance: for the recorded stream, the baseline is written down: interval percentiles, the smooth offset's range and per-frame change, and the share of held snapshots.

### O2. Interpolation, and the role's defaults, for new VMC subjects

- `EnsureSubjectSettingsWithDefaults` gives the new subject what Live Link would: the role's settings class, its pre-processors and its interpolation processor from **Project Settings → Live Link** (`ULiveLinkSettings::GetDefaultSettingForRole`), else the project-wide processor if it suits the role, as `PushSubjectStaticData_Internal` does. If no processor resolves for the animation role (see "Not known yet"), use `ULiveLinkAnimationFrameInterpolationProcessor`, which blends bone transforms. Objects are created with the new settings as their outer, as the remapper is, so `CreateSubject` copies them. A subject that already exists (a preset, the user's settings) keeps its own.
- Test: a new VMC subject has `ULiveLinkAnimationFrameInterpolationProcessor` (the class, not just any processor); an existing subject's choice is kept.

Acceptance: measured with O1 on the recording; expected to remove most of the stepping, since Live Link's smooth offset already keeps the read time behind the newest frame.

### O3. A steady read time (as S-1 decides)

**Status, revised 2026-10-08 (S-1):** on the real loaded recording (`C:\uep\rec\xr_loaded.vmcrec`, kept out of the repository: 45.6 fps, half the intervals under 2 ms, 56 gaps over 100 ms), the model gave, with a 30 ms Engine Time Offset unless noted:

| | Held | Read time back | Synthetic traces with a step back over 20 ms |
|---|---|---|---|
| Arrival times, no offset (before #203) | 33.7% | 3 | 9 of 60 |
| Arrival times + 30 ms (#203's default offset) | 14.3% | 3 (max 39 ms) | 9 of 60 |
| Steady frame times + 30 ms (#203) | 34.6% | 0 | 0 |
| O3b: smoothing off + 50 ms | 16.7% | 0 | 0 |
| O3b: smoothing off + 66 ms | 10.8% | 0 | 0 |

Most of #203's gain is its 30 ms offset. Steady frame times are now off by default, and O3b is the opt-in project setting.

Live check, 2026-10-08 (XR Animator, editor capped at 30 fps, `VMC.Stats` over 12 s): at about 88 ms total latency, smoothing on with a 50 ms offset held 4.9% of ticks with one step back and smooth-offset jumps up to 56 ms; smoothing off with a 75 ms offset held 8.1% with no step back and a constant delay. The owner saw it smoother, with no lurching. Recommended Engine Time Offset with the setting: 0.05 to 0.075 s.

**Earlier status:** O3a is implemented (`FVMCFrameClock`, the source setting **Steady Frame Times**, on by default; the VMC source settings default **Engine Time Offset** to 30 ms and **Buffer Size (Frames)** to 30; `VMC.Stats` prints arrival minus frame time). The clock was tuned on a model of Live Link's timing (`VMC.FrameClock.LiveLinkReadTime`: its smooth offset and clock offset estimator, copied from the engine), over traces shaped like the baseline: no read-time step back over 20 ms (raw arrival times: 17 of 120 traces, up to 91 ms), held reads about a fifth fewer, and on a healthy stream a smooth offset that no longer moves. Two bounds came out of it: a frame's time is never more than 0.2 s behind its arrival (else the clock restarts) or 0.15 s ahead of it (the rest of a burst after a restart gets times 0.1 ms apart), because Live Link's clock offset estimate snaps when it is 0.25 s off for 6 frames, and the snap steps the read time back. Gaps of hundreds of milliseconds still hold the avatar: no delay the owner would accept hides them.

- **O3a, steady stamps in the source (recommended if O1 confirms finding 2):** the source stamps each frame on a smoothed clock rather than its raw arrival time: for example a running estimate of the sender's interval, with each stamp `max(previous stamp + a minimum step, smoothed arrival)`, or the sender's own time (`/VMC/Ext/T`) when it sends one, mapped by Live Link's clock-offset estimator. The intervals Live Link sees are then steady, so its smooth offset stops jittering, and the latency stays Live Link's ~1.5 frames.
- **O3b, a fixed delay:** the VMC source defaults its **Engine Time Offset** to a fixed value, with the smooth offset turned off by `LiveLink.TimedDataInput.NumFramesForSmoothOffset 0`, a project-wide console variable that affects every Live Link source. The total latency is then the fixed delay plus the clock offset.
- Either way: the source's frame buffer (`MaxNumberOfFrameToBuffered`, 10 by default) covers the delay at the sender's rate. At 120 Hz, 10 frames are 83 ms, too few for about 70 ms of delay plus margin, so the VMC default rises (for example to 30).
- Test, through the path the avatar uses: a test hook that takes the arrival time (`OnPacket` does; `InjectPacketForTest` stamps `FPlatformTime::Seconds()`), frames stamped near the current time (Live Link's clock-offset estimator jumps to the first sample's offset, so stamps far from now are offset), `ForceTick`, then `EvaluateFrame_AnyThread` (the snapshot from `Update`, which applies the smooth offset; `EvaluateFrameAtWorldTime_AnyThread` doesn't). Expect a pose between two received ones.
- Docs: the VMC README's source settings and troubleshooting ("choppy motion"), with the total latency stated.

Acceptance: on the recording, the share of held snapshots near 0 and the read time moving forward every tick; latency written down.

### O4. Spring bones between steps

- `FVRMSpringSolver` blends between the previous and the latest step's tails by the time left over, so the tails move every rendered frame at any rate. Test: at 144 Hz and at a varying rate, the tails' motion between frames has no zero-motion frames while the head moves.

### O5. The avatar ticks every frame

- Check the generated actor template's skeletal mesh settings (visibility-based anim tick option, update rate optimisations). Change only what skips or slows animation updates for a Live Link avatar; write down what each setting is and why.

### O6. Background throttling

- Troubleshooting entries (User Guide and VMC README): the viewport stops while another app is focused → **Use Less CPU when in Background**.
- Optional: a hint in the source's status line when the editor is throttled. That needs UnrealEd, so it belongs in `VMCLiveLinkEditor`.

### O7. Sender settings

- What XR Animator (and VSeeFace, VirtualMotionCapture) offers that affects the rate and the jitter: camera frame rate, smoothing, send rate. Recommendations in the User Guide, checked with each sender's current version.

### O8. Editor checks

- A smoothness check: stream (XR Animator, or the recording), editor focused, Simulate; the avatar and its springs move without visible steps; record the O1 numbers. On UE 5.6, 5.7 and 5.8, at 60 Hz and at another refresh rate.

## 4. Not in scope

- CPU optimisation of the plugins: the measured costs are far below a frame's budget. The benchmarks stay as regression guards.
- Changing how Live Link evaluates subjects (engine code). O3b only sets an engine console variable, and only if S-1 chooses it.
