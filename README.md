# clap-validator-plugin

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
| Validator Effect | `effect` | audio-ports (stereo in/out), params, state, latency, tail, render | **note-ports** | gain |
| Validator NoteFX | `notefx` | note-ports (1 in / 1 out), params, state | **audio-ports — the extension is not implemented at all** | note transpose |
| Validator Synth | `synth` | note-ports (1 in), audio-ports (1 stereo out), params, state, voice-info | latency, tail | 8-voice sine |
| Validator Sidechain Synth | `sidechain-synth` | note-ports (1 in), audio-ports (**non-main** stereo in "Sidechain" + main stereo out), params, state | latency, tail, voice-info | sine gated by sidechain level |
| Validator MultiOut Gen | `multiout-gen` | audio-ports, audio-ports-config ("Stereo": 1 out; "Multi Out": main + 4 aux), params, state | note-ports | distinct sine pitch per port |
| Validator MultiOut FX | `multiout-fx` | audio-ports (1 stereo in), audio-ports-config ("Stereo": 1 out; "Multi Out": main + 2 aux), params, state | note-ports | input fan-out |

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

Planned future flavors: `audioports-zero` (extension present, zero ports), `surround`,
`gui`, `preset` (preset-load), `slow` (slow state save / high latency), `hostcheck`
(aggressively exercises host extensions).

## License

MIT — see [LICENSE](LICENSE).
