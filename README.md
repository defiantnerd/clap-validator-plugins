# clap-validator-plugin

A suite of [CLAP](https://github.com/free-audio/clap) plugins designed to test **hosts** —
the inverse of [clap-validator](https://github.com/free-audio/clap-validator), which tests plugins.

Each plugin in the suite deliberately exposes a different extension profile (an audio effect
with *no* note-ports, a pure note transformer with *no* audio-ports extension at all, a synth,
multi-output configurations, an instrument with a non-main sidechain input, …), forcing hosts
to correctly handle heterogeneous plugin configurations. In debug builds the plugins also
actively detect host misbehavior (thread-contract violations) and report it through the host's
`clap.log` extension.

## Building

```sh
cmake --preset default
cmake --build --preset default
```

The suite builds as a single `clap-validator-plugins.clap` binary exposing all plugins
through one `clap_plugin_factory`.

To copy the built plugin into your user CLAP folder:

```sh
cmake --build --preset default --target copy-to-clap-folder
```

*(Plugin matrix documentation follows as the suite grows.)*
