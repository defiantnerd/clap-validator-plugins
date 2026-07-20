# clap-validator-plugins

[![build](https://github.com/defiantnerd/clap-validator-plugins/actions/workflows/build.yml/badge.svg)](https://github.com/defiantnerd/clap-validator-plugins/actions/workflows/build.yml)

A suite of [CLAP](https://github.com/free-audio/clap) plugins designed to test **hosts** —
the inverse of [clap-validator](https://github.com/free-audio/clap-validator), which tests plugins.

All thirteen plugins ship in a single `clap-validator-plugins.clap` binary whose entry point
exposes **two factories**: the plugin factory and a preset-discovery factory — multi-plugin
binaries and multi-factory entries are both things hosts must handle. Each plugin deliberately
exposes a different extension profile, forcing hosts to correctly handle heterogeneous plugin
configurations.

The suite is also an *active* diagnostic instrument:

- Every plugin logs the **host's identity** (name, vendor, version, URL, CLAP version) and
  probes **22 host-side extensions** at `init()`, logging which are present and which are not —
  a one-glance host fingerprint.
- Every plugin monitors the host against the **calling-sequence contracts** of the CLAP spec —
  see [Host contract checking](#host-contract-checking) below; the complete list of probed
  violations lives in [docs/host-contract-violations.md](docs/host-contract-violations.md).
- All log output of an instance flows through a per-instance `LogBuffer` (in addition to
  `clap.log`/stderr), which the GUI flavor renders live in its log pane.

## Host contract checking

Each plugin instance runs a `ContractMonitor` that checks the host against ~40 documented
calling-sequence rules from the CLAP 1.2.10 headers, always on (release builds included):

- **lifecycle order** — double `activate()`, `process()` outside `start/stop_processing`,
  `destroy()` while active, spurious `on_main_thread()`, concurrent `process()` calls, …
- **process-time data** — `frames_count` vs the activate bounds, `steady_time` monotonicity,
  unsorted/malformed/out-of-block events, buffer counts vs the declared port layout, unknown
  param ids, `params.flush()` overlapping `process()`;
- **per-extension rules** — GUI call order and embedded/floating misuse, port scans or
  config switches while active, latency/voice-info queries in illegal states, and more;
- **DSO entry rules** — factory access before `entry.init()`, unbalanced or concurrent
  init/deinit (carried by a process-global early log into every instance's log pane);
- **thread contracts** — via the host's `clap.thread-check` when available (authoritative,
  debug-assert), init-thread heuristics otherwise (log-only).

Every finding is logged with a **stable code** (`seq [L03] activate(): called while already
active`) at `CLAP_LOG_HOST_MISBEHAVING`, throttled per code so audio-path detectors can't
flood the log. Violating calls are refused only where the spec makes rejection safe;
everything else is tolerated so the session keeps running. Detections surface four ways:

1. the **log** (GUI pane, host `clap.log`, stderr);
2. the GUI flavor's live **badge** — green `contract: OK` / red `contract: N violations [last L03]`;
3. a **destroy-time summary**: `contract summary: 3 violations (L03×1, P02×2)`;
4. a machine-readable **query extension** `org.clap-validator.violations/1`
   ([include/cvp/violations.h](include/cvp/violations.h)) exposing per-code counters and the
   last message per code to test harnesses — served by every flavor.

The full catalog — each code, the spec rule behind it, how it is detected, the response
policy, and the list of contracts that are provably *undetectable* from inside a plugin —
lives in [docs/host-contract-violations.md](docs/host-contract-violations.md).

## The plugin matrix

| Plugin | id (`org.clap-validator.…`) | Extensions exposed | Deliberately absent | DSP |
|---|---|---|---|---|
| Validator Effect | `effect` | audio-ports (stereo in/out, **32+64-bit, common sample size required**), params, state, latency, tail, render, remote-controls | **note-ports** | gain |
| Validator NoteFX | `notefx` | note-ports (1 in / 1 out), params, state | **audio-ports — the extension is not implemented at all** | note transpose |
| Validator Synth | `synth` | note-ports (1 in), audio-ports (1 stereo out, **supports + prefers 64-bit**), params (Volume **polyphonically modulatable** per note/key/channel), state, voice-info | latency, tail | 8-voice sine |
| Validator Sidechain Synth | `sidechain-synth` | note-ports (1 in), audio-ports (**non-main** stereo in "Sidechain" + main stereo out), params, state, remote-controls | latency, tail, voice-info | sine gated by sidechain level |
| Validator MultiOut Gen | `multiout-gen` | audio-ports, audio-ports-config ("Stereo": 1 out; "Multi Out": main + 4 aux), params, state | note-ports | distinct sine pitch per port |
| Validator MultiOut FX | `multiout-fx` | audio-ports (1 stereo in), audio-ports-config ("Stereo": 1 out; "Multi Out": main + 2 aux), params, state | note-ports | input fan-out |
| Validator AudioPortsZero | `audioports-zero` | **audio-ports with 0 ports** in both directions, params, state | note-ports | none (sleeps) |
| Validator Slow | `slow` | audio-ports (stereo in/out), params, state, latency | note-ports | passthrough |
| Validator HostCheck | `hostcheck` | audio-ports (stereo in/out) **only** | **params, state** — first flavor with neither | passthrough |
| Validator Params | `params` | audio-ports (stereo in/out), **params (all flag types)**, state, remote-controls | note-ports, latency, tail, render | gain/drive/rotation + input meter |
| Validator GUI | `gui` | audio-ports (stereo in/out), params, state, **gui**, remote-controls (**2 pages**) | note-ports | gain |
| Validator Preset | `preset` | audio-ports (stereo in/out), params (Gain, Color), state, **preset-load**, remote-controls | note-ports | gain |
| Validator Surround | `surround` | audio-ports (surround-typed main in/out), **surround**, **configurable-audio-ports** (Quad 4.0 / 5.1 / 7.1), params (Gain, Solo Channel), state, remote-controls | note-ports, **mono/stereo entirely** | passthrough with channel solo |

Host-testing traps baked in:

- **Effect / Latency parameter**: changing it while active triggers `host->request_restart()`;
  the new value is reported via `clap.latency` after reactivation. Dynamic latency is a classic
  host weak spot.
- **NoteFX**: `get_extension("clap.audio-ports")` returns `NULL`. Hosts that assume every
  plugin has audio ports must cope.
- **Sidechain Synth**: an *instrument* with a non-main audio input. Correct routing is audibly
  verifiable — the sidechain input gates the synth output.
- **MultiOut Gen/FX**: port ids stay stable across configs (the main out keeps id 0 in both);
  each Gen output port emits a distinct pitch (A3, C#4, E4, G4, A4), so routing is verifiable
  by ear or meter. `select()` correctly fails while the plugin is active.
- **Params (parameter-type zoo)**: one parameter per flag constellation — automatable+
  modulatable Gain with the suite's only **non-null cookie** (a wrong cookie back from the
  host is flagged as P11), non-automatable Drive (live changes legal, automation while the
  transport plays is flagged as P14), the **read-only** "In Peak" input meter (host writes
  flagged as P12 and ignored), a **bypass**, the **periodic** Rotation (0–360° stereo
  rotation), a **requires-process** DC Offset (the params.h example), a stepped **enum**
  Curve, and a **hidden** Secret Trim. Illegal `PARAM_MOD`s are flagged as P13. The synth's
  Volume is **polyphonically modulatable**: note/key/channel-addressed `PARAM_MOD` events
  modulate single voices, audibly.
- **Sample sizes (32/64-bit)**: deliberately heterogeneous. The Effect supports both formats
  but declares `REQUIRES_COMMON_SAMPLE_SIZE` (mixing in/out formats is flagged as P10, yet
  tolerated with conversion); the Synth **supports and prefers** 64-bit; the Sidechain Synth
  and every other flavor are 32-bit-only — hosts must check per plugin *and per port*. Handing
  a 64-bit buffer to a non-supporting port is flagged (P09) and the block rejected.
- **Process-status hints**: the voice-based synths return `CLAP_PROCESS_SLEEP` when no voice
  is sounding; MultiOut Gen returns `CLAP_PROCESS_CONTINUE_IF_NOT_QUIET` while its Level is
  zero — hosts get both silence-optimization paths to prove they wake the plugins again.
- **AudioPortsZero**: the counterpart to NoteFX — the audio-ports extension is *present* but
  reports zero ports in both directions.
- **Slow**: `state save/load` block for the `Slowness` parameter's duration (default 2 s),
  `activate` blocks 250 ms, latency reports one full second. Hosts must not freeze or time out.
- **HostCheck**: exercises `host->request_callback()` and logs the `on_main_thread()`
  round-trip, with lifecycle logging enabled so every transition the host drives is recorded.
  Also deliberately has neither params nor state — the only flavor with neither.
- **Remote controls**: every plugin with more than one parameter exposes
  `clap.remote-controls/2` (registered under the compat id too). The GUI flavor has **two**
  pages ("Mix": Gain+Mute, "Options": Mode), so hosts must handle page switching; unused
  control slots are correctly `CLAP_INVALID_ID`.
- **Preset**: the binary exposes a **second factory** (`clap.preset-discovery-factory/2`, plus
  compat id) — hosts must handle multi-factory entries. Its provider declares **both location
  kinds**: internal factory presets (PLUGIN kind, loaded by `load_key`: `internal:unity`,
  `internal:quiet`, `internal:loud`) and `.cvpreset` text files (FILE kind) crawled from the
  per-user preset directory (`~/Library/Application Support/clap-validator-plugin/presets`,
  `%APPDATA%\clap-validator-plugin\presets`, `$XDG_DATA_HOME/clap-validator-plugin/presets`).
  Two sample files are created there on first discovery (never overwritten). Loading applies
  the values, calls `clap_host_params.rescan(CLAP_PARAM_RESCAN_VALUES)` and notifies
  `clap_host_preset_load.loaded()`; failures report through `on_error()`.
- **Surround**: `is_channel_mask_supported()` accepts exactly the three layout masks — mono and
  stereo are rejected, in the mask check and in `configurable-audio-ports` requests alike, so
  hosts cannot fall back to stereo. The host switches layouts (deactivated only) via
  `configurable-audio-ports`; requesting one side switches both (ports stay symmetric), and
  supplied channel maps must match the plugin's. The **Solo Channel** parameter selects a
  surround channel *identifier* (FL/FR/FC/LFE/BL/BR/SL/SR), not an index — soloing FC means
  front-center in every layout, so a wrong host channel map is immediately audible.
- **GUI**: see below.

## The GUI

The `Validator GUI` plugin implements `clap.gui` with **zero framework/library dependencies**:
plain Cocoa on macOS (Objective-C++), raw Win32 (user32/gdi32/comctl32) on Windows, and raw
Xlib on Linux. Each platform view shows:

- the plugin's **parameters at the top** — editable sliders that emit proper CLAP
  `gesture_begin` / `param_value` / `gesture_end` events through a thread-safe queue plus
  `clap_host_params.request_flush()`, so the host's GUI→automation path gets exercised;
- below, a **≥20-line log pane in a monospaced font**. *All* log output of the instance is
  guaranteed to appear there: every logging path (host `clap.log` delivery, stderr fallback,
  thread-check violation reports, lifecycle events, the host fingerprint) flows through a
  per-instance `LogBuffer` first, and the view renders from that buffer at ~10 Hz. Every
  `gui_*` call the host makes is logged too, so the editor live-documents the host's GUI
  protocol usage;
- a **Copy Log** button between the two sections that copies the complete log to the system
  clipboard (NSPasteboard / `CF_UNICODETEXT` / X11 `CLIPBOARD` selection with
  TARGETS/UTF8_STRING service), next to the live **contract badge** (green `contract: OK`,
  red `contract: N violations [last CODE]`) fed by the host-contract monitor.

Embedded windows only (per spec recommendation); floating-window requests are rejected and
logged. Platform status: **macOS build- and runtime-verified** (embedded in a test host,
screenshot-checked); **Windows cross-compile-verified** with MinGW; **Linux/X11
compile-verified** against Xlib headers — runtime verification on real Windows/Linux hosts
pending. On Linux the flavor is compiled out (with a CMake status message) if X11 development
headers are absent.

## Building

Requires CMake ≥ 3.24, Ninja (or another generator), and a C++20 compiler.
The CLAP SDK (pinned via `CVP_CLAP_TAG`, default `1.2.10`) is fetched at configure time.

```sh
cmake --preset default        # Debug
cmake --build --preset default
```

Copy the built plugin into your user CLAP folder (`~/Library/Audio/Plug-Ins/CLAP`,
`%LOCALAPPDATA%\Programs\Common\CLAP`, or `~/.clap`):

```sh
cmake --build --preset default --target copy-to-clap-folder
```

Or pass `-DCVP_COPY_AFTER_BUILD=ON` to copy automatically after every build.

## Testing

With [clap-validator](https://github.com/free-audio/clap-validator) on your `PATH` (or passed
via `-DCLAP_VALIDATOR=/path/to/clap-validator` at configure time):

```sh
ctest --preset default
```

[clap-info](https://github.com/free-audio/clap-info) is useful to inspect each plugin's exact
extension/port/param profile.

CI (GitHub Actions) builds the suite on macOS, Windows, and Linux, runs clap-validator against
each build, and uploads the built `.clap` for every platform as workflow artifacts.

## Options

| CMake option | Default | Effect |
|---|---|---|
| `CVP_CLAP_TAG` | `1.2.10` | free-audio/clap tag to build against |
| `CVP_THREAD_CHECKS` | `ON` | runtime `[main-thread]`/`[audio-thread]` assertions (host-misbehavior detector) |
| `CVP_COPY_AFTER_BUILD` | `OFF` | copy the `.clap` to the user plugin folder after each build |

## Adding a plugin flavor

1. Create `src/plugins/<name>/<name>.h/.cpp` — subclass `cvp::Plugin`, inherit the
   `cvp::ext::*Provider` interfaces you want, and register each one with
   `provideExtension()` in the constructor. Only registered extensions are ever
   returned from `get_extension()` — deliberate omission is the default.
2. Add the source file to `CMakeLists.txt` and one `{&Class::descriptor, &Class::create}`
   line to `src/registry.cpp`.

The wrapper (`src/wrapper/`) already provides Provider interfaces for: audio-ports,
audio-ports-config, configurable-audio-ports, gui, latency, note-ports, params, preset-load,
remote-controls, render, state, surround, tail, voice-info — plus the `LogBuffer`,
`ThreadChecker`, `StreamHelper`, and `ParamEventQueue` utilities.

## License

MIT — see [LICENSE](LICENSE).
