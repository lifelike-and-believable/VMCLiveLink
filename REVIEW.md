# Code review checklist

The reviewer agent (`.claude/agents/code-reviewer.md`) reviews every PR against this list before
the PR is marked ready (B.0 rule 14 in `Planning Docs/Code_Review_and_Refactor_Plan_2026-09.md`).

The review looks for defects: code that fails to build, crashes, gives wrong results, loses data
or breaks a project rule. It doesn't report style or naming preferences, or refactors the diff
doesn't need.

## How findings are reported

- Each finding names the file and line, says what is wrong, and gives a **concrete failure
  scenario**: the input, state or build configuration, and what goes wrong. A suspicion without a
  scenario isn't reported.
- Severity:
  - **Blocking:** the build fails in a configuration CI or users build, a crash, wrong output,
    data loss, a thread race, or a broken project rule below.
  - **Optional:** real but minor: a misleading comment, a missing edge-case test, a clearer
    equivalent.
- An engine API the reviewer can't confirm exists, with that signature, in every supported engine
  (UE 5.6, 5.7 and 5.8, or behind a version guard) is reported as
  **Unverified API**, blocking until the author confirms it (rule 2).
- Every finding is fixed or answered on the pull request (its description or a comment) before the
  merge.

## What to check

### 1. Unreal builds

CI builds the Editor target without unity files and a Game target (Development and Shipping)
with them.
- **Unity builds:** helper functions, constants and types in anonymous namespaces or at file scope
  can collide with another `.cpp` in the same module. Prefer names unique to the file, or a named
  namespace.
- **Shadowing is an error** (C4456, C4457, C4458, C4459): locals and parameters named like a
  member (also in lambdas and static member functions), or like a global in a unity neighbour.
- **Module dependencies:** every new include or symbol needs its module in `Build.cs`. Examples:
  `EKeys` needs InputCore, `FMessageLogModule` needs MessageLog, details views need
  PropertyEditor. Editor-only modules in a runtime module belong under `Target.bBuildEditor`.
- **Interchange pipelines:** a pipeline registered in the project settings must be a pipeline asset
  (`/VRMInterchange/DefaultPipelines/...`); Interchange can't instantiate a class path (#184).
- **Shipping and game builds:**
  - Log category objects don't exist without logging (`FNoLoggingCategory`).
  - Editor-only members and functions need `WITH_EDITOR` or `WITH_EDITORONLY_DATA` guards on
    both declaration and use.
  - Development-only test code needs `WITH_DEV_AUTOMATION_TESTS`.
- **Engine APIs:** deprecated calls (C4996 is allowed, but prefer the replacement) and calls whose
  existence or signature in any supported engine (5.6, 5.7, 5.8) is a guess (report as Unverified
  API). An engine difference goes behind a `UE_VERSION_NEWER_THAN_OR_EQUAL` guard whose threshold is
  the version the API appeared in, with a comment naming the change; the older branch keeps the
  older code.
- **Content:** a `.uasset` or `.umap` saved from 5.7 or 5.8 can't be loaded by 5.6 (CI's content
  check fails it).
- **Headers:** every new file starts with the copyright header CI checks.

### 2. Threads and lifetimes

- **Which thread runs each function.** The VMC receive thread, the game thread, and Interchange
  worker threads for translators and payloads. UObjects are touched only on the game thread.
- **Locks:** each shared member is read and written under the lock that guards it. No lock is
  held while calling out to code that can take it again (logging, delegates, output devices).
- **UObject lifetime:** raw UObject pointers kept across frames without a strong reference or
  `UPROPERTY` can be collected by GC.
- **Unbinding:** tickers, delegates and output devices bound to `this` are removed in shutdown
  or the destructor.

### 3. Saved data

New, renamed or removed `UPROPERTY` fields, and changed defaults or meanings. Assets saved before
the change must still load and behave as documented (rule 5). Plain additions of tagged
properties need no version bump, but a changed meaning does.

### 4. Specs

VRM 0.x and 1.0, MToon, glTF and VMC/OSC behaviour must match the spec or the reference
implementations (UniVRM, three-vrm). Check defaults, units (metres or centimetres, degrees or
radians), axis conversions and index bases.

### 5. Tests

- Does the test fail if the change is reverted? Watch for tests that pass trivially.
- **Warnings:** a warning logged during a test breaks the 0-warning rule (checked by hand on CI's
  `Tests:` line; CI itself stays green). An expected one
  (`AddExpectedError`) reaches other output devices demoted to Verbose.
- **Shared runner:** don't assert on timing, on free ports without a fallback, or on project
  `Config/*.ini` values (rule 11).
- **Leftover state:** tickers, singletons and settings changed by a test must not leak into later
  tests.

### 6. Project rules

- **D-4:** VMCLiveLink and VRMInterchange ship separately. Neither may depend on the other, or on
  a plugin that doesn't ship with Unreal (Fab rules).
- **No startup edits:** the editor never changes user config or content at startup. Imports
  never save packages; they mark them dirty.
- **Logging:** use the plugin log categories (no new `LogTemp`), and send import problems to the
  VRM Import message log.
- **Docs:** if behaviour changes, the plugin's README (and the User Guide, if it covers it) and
  `CHANGELOG.md` say so in the same PR (rule 9).
