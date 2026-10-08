# VMC smoothness and performance plan

October 2026. With XR Animator streaming to a VRM avatar, the motion looks choppy although the VMC source reports receiving well over 30 fps. This plan says why, what to measure, and what to change, as pull requests with acceptance criteria.

## 1. What we know

### Measured

| What | Value | Where from |
|---|---|---|
| XR Animator's frame rate at the source | 15 to 27 fps (lower while the machine was building) | the source's status line and `VMC.Stats`, 2026-10-07 |
| Jitter (mean deviation of the frame interval) | about 31 ms at 27 fps, so roughly one frame interval | the same |
| Packets per frame | about 4 (bones, blend shapes, apply, OK), 55 bones and about 16 curves | `VMC.Stats` |
| Building a VMC frame (55 bones, 60 curves) | 17 µs for the messages, 1 µs for the frame | `VMC.Perf.StreamFrame` |
| Spring solver, 200 joints and 30 colliders | 0.13 ms per frame | `VRM.Perf.SpringSolver` |

The plugins' own CPU cost is tiny: well under 1% of a 60 fps frame. The choppiness isn't a CPU problem in our code; it's a timing problem, in what the avatar shows between poses that arrive irregularly.

### Found in the code (not guesses)

1. **New VMC subjects have no interpolation.** When Live Link creates a subject from pushed static data, it gives it the role's default interpolation processor from the Live Link project settings (`LiveLinkClient.cpp`, `PushSubjectStaticData_Internal`). Our source creates its subject itself, in `FVMCLiveLinkSource::EnsureSubjectSettingsWithDefaults`, with a fresh `ULiveLinkSubjectSettings` and no processor. The Live Link panel shows **Interpolation: None** for `VMC_Subject`. Without interpolation, Live Link evaluates the closest frame, so the avatar holds each pose until the next one, at the sender's rate (about 27 fps) and with its jitter.
2. **Live Link evaluates at the current time.** The source's **Engine Time Offset** (Live Link's buffer settings) is 0. Even with interpolation, the evaluation time is usually after the newest frame (frames are stamped on arrival), so there is nothing to blend towards and the newest pose is held: the same steps. Blending needs the evaluation time to be behind the newest frame by about one frame interval plus the jitter. Here that's about 37 + 31 ≈ 68 ms.
3. **The editor throttles itself in the background.** With XR Animator focused, the Unreal editor is a background window, and **Editor Preferences → General → Performance → Use Less CPU when in Background** (on by default) slows the whole editor down. The viewport is then choppy whatever the data. This is the user's setting; the plugin must not change it.

### Not known yet

- How much of the jitter comes from XR Animator (camera rate, AI inference time) and how much from the network stack or our receive thread. One computer, loopback, so the network part should be small.
- Whether the generated avatar actor ticks its animation every frame in every case (for example, visibility-based tick options in the template), and how the spring bones look at a variable frame rate.
- How smooth "smooth enough" is, numerically: we have no measure of it yet.

## 2. Decisions

| ID | Decision | Choice |
|---|---|---|
| S-1 | Default evaluation delay (Engine Time Offset) for a new VMC source | **Decided 2026-10-08: about 66 ms** (two frames at 30 fps), adjustable per source. Lower latency (about 40 ms) gives occasional steps when packets bunch up. |
| S-2 | Make the delay adapt to the measured jitter | Not now. A fixed default, plus the measurements in O1, first; revisit if one value can't suit both webcam senders (variable) and VR-tracker senders (steady). |
| S-3 | Change the user's editor settings (background throttling) | No. Document it, and say so in the troubleshooting tables; consider a hint in the source's status line when the editor is throttled (O5). |

## 3. Work

In order. Each item is one pull request following CONTRIBUTING (review, CI on three engines, CHANGELOG, docs).

### O1. Measure smoothness

Before changing the timing, make it measurable, so the effect of each change is a number and not an impression.

- **Arrival statistics:** `VMC.Stats` gains the frame interval's percentiles (p50, p95, max) over its window, next to the mean and jitter it has.
- **Evaluation statistics:** how often Live Link evaluated the subject without a later frame to blend towards (it held a pose). Live Link's buffer statistics (`IsBufferStatsEnabled`: overflow and underflow counts, shown by the Timed Data Monitor plugin) count exactly that. Check them in 5.6 to 5.8, and report them in `VMC.Stats` if they can be read.
- **A recorded stream to replay:** record XR Animator with `python scripts/vmc_sender.py record --out <file>` (it keeps the timing), and replay it with `vmc_sender.py send`, so every change is compared on the same input. Keep the recording out of the repository (decision D-8) unless it is our own capture.

Acceptance: for the recorded XR Animator stream, the current numbers are written down (interval percentiles, the share of held evaluations); they are the baseline for O2 and O3.

### O2. Interpolation for new VMC subjects

- `EnsureSubjectSettingsWithDefaults` gives the new subject the interpolation processor Live Link would: the role's default from **Project Settings → Live Link** (`ULiveLinkSettings::GetDefaultSettingForRole`), else the project-wide default if it suits the role, as `PushSubjectStaticData_Internal` does. A subject that already exists (from a preset, or set up by the user) keeps its settings, as now.
- Test: a new VMC subject's settings have the animation role's interpolation processor; an existing subject's choice is kept.

Acceptance: with the recorded stream and the delay at 0, nothing changes visibly (there is still nothing to blend towards); this is the groundwork for O3.

### O3. Default evaluation delay

- The VMC source's settings default **Engine Time Offset** to 0.066 s (S-1). It is Live Link's own buffer setting, so it is already shown in the source's details; the creation panel gains it too, with its unit and what it trades (latency for smoothness).
- Test: evaluating a subject between two received frames, with the delay, gives a pose between them (interpolated), not the older one. Use the source's test hooks (`InjectPacketForTest`) and the client's `EvaluateFrameAtWorldTime_AnyThread`, with frames stamped at known times.
- Docs: the VMC README's source settings and troubleshooting ("choppy motion"), the User Guide.

Acceptance: on the recorded stream, the share of held evaluations (O1) drops to near 0, and the avatar moves smoothly in the editor with the editor focused. The added latency is the delay, about 66 ms.

### O4. The avatar ticks every frame

- Check the generated actor template's skeletal mesh component settings (visibility-based anim tick option, update rate optimizations) and the spring bone node at a variable frame rate (it uses fixed sub-steps, so the result should not depend on the frame rate). Fix anything that skips or slows animation updates for a Live Link avatar.
- Acceptance: written down what each setting is and why; a change only where one causes skipped updates.

### O5. Background throttling

- Troubleshooting entries (User Guide and VMC README): choppy motion while another app is focused → the editor's **Use Less CPU when in Background**.
- Optional: the source's status line notes when the editor is being throttled (if it can be read without changing it).

Acceptance: the docs say it, with the menu path.

### O6. Sender settings

- What XR Animator (and VSeeFace, VirtualMotionCapture) offers that affects the rate and the jitter: camera frame rate, smoothing, send rate. Recommendations in the User Guide's sender section.

Acceptance: written recommendations, checked with each sender's current version.

### O7. Editor checks

- A new editor check for smoothness: stream (XR Animator, or the recorded file), editor focused, Simulate; the avatar moves without visible steps. Record the O1 numbers. On UE 5.6, 5.7 and 5.8.

## 4. Not in scope

- CPU optimization of the plugins: the measured costs (section 1) are far below a frame's budget. The benchmarks stay as regression guards.
- Changing how Live Link evaluates subjects (engine code).
