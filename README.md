# clap-validator-plugins

A suite of [CLAP](https://github.com/free-audio/clap) plugins designed to test **hosts** —
the inverse of [clap-validator](https://github.com/free-audio/clap-validator), which tests plugins.

All plugins ship in a single `clap-validator-plugins.clap` binary exposing them through one
`clap_plugin_factory` (which itself exercises hosts' multi-plugin-binary handling). Each plugin
deliberately exposes a different extension profile, forcing hosts to correctly handle
heterogeneous plugin configurations. In debug builds the plugins also actively detect host
misbehavior — thread-contract violations and illegal lifecycle transitions — and report it
through the host's `clap.log` extension with `CLAP_LOG_HOST_MISBEHAVING`.

## The plugin matrix

| Plugin | id (`org.clap-validator.…`) | Extensions exposed | Deliberately absent | DSP |
|---|---|---|---|---|
| Validator Effect | `effect` | audio-ports (stereo in/out), params, state, latency, tail, render, remote-controls | **note-ports** | gain |
| Validator NoteFX | `notefx` | note-ports (1 in / 1 out), params, state | **audio-ports — the extension is not implemented at all** | note transpose |
| Validator Synth | `synth` | note-ports (1 in), audio-ports (1 stereo out), params, state, voice-info | latency, tail | 8-voice sine |
| Validator Sidechain Synth | `sidechain-synth` | note-ports (1 in), audio-ports (**non-main** stereo in "Sidechain" + main stereo out), params, state, remote-controls | latency, tail, voice-info | sine gated by sidechain level |
| Validator MultiOut Gen | `multiout-gen` | audio-ports, audio-ports-config ("Stereo": 1 out; "Multi Out": main + 4 aux), params, state | note-ports | distinct sine pitch per port |
| Validator MultiOut FX | `multiout-fx` | audio-ports (1 stereo in), audio-ports-config ("Stereo": 1 out; "Multi Out": main + 2 aux), params, state | note-ports | input fan-out |
| Validator AudioPortsZero | `audioports-zero` | **audio-ports with 0 ports** in both directions, params, state | note-ports | none (sleeps) |
| Validator Slow | `slow` | audio-ports (stereo in/out), params, state, latency | note-ports | passthrough |
| Validator HostCheck | `hostcheck` | audio-ports (stereo in/out) **only** | **params, state** — first flavor with neither | passthrough |
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
- **AudioPortsZero**: the counterpart to NoteFX — the audio-ports extension is *present* but
  reports zero ports in both directions.
- **Slow**: `state save/load` block for the `Slowness` parameter's duration (default 2 s),
  `activate` blocks 250 ms, latency reports one full second. Hosts must not freeze or time out.
- **HostCheck**: probes ~22 host-side extensions on `init` and logs present/absent, exercises
  `request_callback()`, and records every lifecycle transition. Also deliberately has neither
  params nor state.
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
  thread-check violation reports, lifecycle events) flows through a per-instance `LogBuffer`
  first, and the view renders from that buffer at ~10 Hz. Every `gui_*` call the host makes is
  logged too, so the editor live-documents the host's GUI protocol usage.

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

With [clap-validator](https://github.com/free-audio/clap-validator) on your `PATH`:

```sh
ctest --preset default
```

[clap-info](https://github.com/free-audio/clap-info) is useful to inspect each plugin's exact
extension/port/param profile.

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

Planned future flavors: `surround`, `preset` (preset-load + preset-discovery factory).

## License

MIT — see [LICENSE](LICENSE).
