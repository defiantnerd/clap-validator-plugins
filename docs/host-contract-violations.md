# Host calling-sequence violations

Every plugin in this suite monitors the host against the calling contracts
documented in the CLAP 1.2.10 headers. Detected violations are:

- **logged** through the plugin's unified log path (GUI log view → host
  `clap.log` → stderr fallback) as `CLAP_LOG_HOST_MISBEHAVING`, in a
  grep-stable format: `seq [L03] activate(): called while already active`;
- **counted** per stable code, queryable via the custom extension
  `org.clap-validator.violations/1` ([include/cvp/violations.h](../include/cvp/violations.h));
- **shown** live in the GUI flavor's badge (`contract: OK` /
  `contract: N violations [last CODE]`);
- **summarized** at `destroy()`: `contract summary: 3 violations (L03×1, P02×2)`.

Log lines are throttled per code (occurrences 1–5 in full, then every 100th
as a repeat note) so audio-path detectors cannot flood the log; counters
always increment. Sequence checks never `assert` — only authoritative
thread-contract findings (confirmed by the host's own `clap.thread-check`)
abort in debug builds.

**Response policy** per check: *Reject* = the call is refused with a
spec-safe return value; *Tolerate* = logged, then handled as gracefully as
possible so the session keeps running.

## E — DSO entry & factories (`entry.h`, `factory/plugin-factory.h`)

These occur before/without a plugin instance, so they are carried by a
process-global ring ("EarlyLog"): printed to stderr immediately and copied
into every plugin instance's log at `init()`. They have no per-instance
counter and do not appear in the query extension or the destroy summary.

| Code | Violation | Spec | Response |
|---|---|---|---|
| E01 | `get_factory()` or a factory method used before `entry.init()` returned true (or after `deinit()`) | entry.h: init "must be called first, before any-other CLAP-related function" | Tolerate — the factory is returned anyway (defensive behavior required since CLAP 1.2.0) |
| E02 | `entry.init()` called again without a matching `deinit()` | entry.h: "must only be called once, until a later call to deinit()" | Tolerate — ref-counted |
| E03 | `entry.deinit()` without a matching successful `init()` | entry.h: "for every init() which returns true, one deinit() should be called" | Tolerate — no-op |
| E04 | `init()`/`deinit()` overlapping another entry call or any other DSO symbol | entry.h: "forbidden to call it simultaneously with *any* other CLAP-related symbols" | Tolerate — detected with atomic depth counters (entry calls may legally run on any thread, so thread identity proves nothing) |
| PD01 | preset-discovery indexer calls the provider before `provider.init()`, re-enters it from a `declare_*()` call, or double-inits it | factory/preset-discovery.h: "Don't callback into the provider during this call"; "forbidden to call back into the indexer before init()" | Tolerate |

## L — Core plugin lifecycle (`plugin.h`)

State machine: created → initialized (`init`) → active (`activate`) →
processing (`start_processing`), walked back by `stop_processing` /
`deactivate` / `destroy`.

| Code | Violation | Spec | Response |
|---|---|---|---|
| L01 | `get_extension()` before `init()` | plugin.h: "forbidden to call it before plugin->init()" (the plugin's own `org.clap-validator.violations` extension is exempt — it must stay reachable for harnesses) | Tolerate — the extension is served |
| L02 | `init()` called twice on the same instance | implied by the state machine | Reject — returns true without re-running init logic |
| L03 | `activate()` while already active | plugin.h: `[main-thread & !active]` | Reject (false) |
| L04 | `deactivate()` while not active | plugin.h: `[main-thread & active]` | Tolerate — no-op |
| L05 | `deactivate()` while still processing (missing `stop_processing()`) | processing implies active; stop must come first | Tolerate — processing is force-stopped, then deactivates |
| L06 | `destroy()` while still active | plugin.h: "It is required to deactivate the plugin prior to this call" | Tolerate — defensive teardown |
| L07 | `start_processing()` while not active, or while already processing | plugin.h: `[audio-thread & active & !processing]` | Reject (false) / Tolerate (true) |
| L08 | `stop_processing()` while not processing or not active | plugin.h: `[audio-thread & active & processing]` | Tolerate — no-op |
| L09 | `process()` while not active or not processing | plugin.h: `[audio-thread & active & processing]` | Reject (`CLAP_PROCESS_ERROR`) |
| L10 | `reset()` while not active | plugin.h: `[audio-thread & active]` | Tolerate |
| L11 | `on_main_thread()` without a pending `host.request_callback()` | plugin.h: "in response to a previous call to request_callback" (coalescing multiple requests into one callback is legal and not flagged) | Tolerate |
| L12 | `process()` called concurrently on two threads | thread-check.h: audio-thread functions "ARE NOT CONCURRENT" | Reject — the second call gets `CLAP_PROCESS_ERROR` |
| L13 | `activate()` with `min_frames < 1` or `max < min` | plugin.h: frame count "bounded by [1, INT32_MAX]" | Tolerate — clamped |

## P — Process-time data contracts (`process.h`, `events.h`, `params.h`)

| Code | Violation | Spec | Response |
|---|---|---|---|
| P01 | `frames_count` outside the `[min, max]` bounds from `activate()` | plugin.h: "process's frame count will be included in the [min, max] range" | Tolerate |
| P02 | `steady_time` went backwards (must advance by ≥ the previous `frames_count`; -1 = unavailable). Re-baselined after `reset()` and `start_processing()` | process.h: "must be increased by at least frames_count for the next call" | Tolerate |
| P03 | input events not sorted by `time` | process.h: "The host will deliver these sorted in sample order" | Tolerate |
| P04 | event `time >= frames_count` (beyond the block) | event time is a sample offset within the block | Tolerate |
| P05 | `audio_inputs_count`/`audio_outputs_count` or a buffer's `channel_count` differs from the declared port layout | process.h: buffers "must have the same count as specified by clap_plugin_audio_ports->count()" | Tolerate |
| P06 | event `header.size < sizeof(clap_event_header)` (malformed) | events.h | Tolerate — event skipped |
| P07 | `params.flush()` concurrent with a running `process()` | params.h: "must not be called concurrently to clap_plugin->process()" | Tolerate |
| P08 | `CLAP_EVENT_PARAM_VALUE`/`PARAM_MOD` targeting an unknown `param_id` | params.h event routing | Tolerate — event ignored |

## T — Thread contracts (`thread-check.h` + per-function annotations)

T01/T02 come from the existing ThreadChecker. Findings confirmed by the
host's own `clap.thread-check` extension are *authoritative* (counted +
debug-assert); without that extension, main-thread checks fall back to an
init-thread heuristic that is logged but never counted (a host may legally
drive the plugin from another thread), and audio-thread checks are skipped.
T03/T04 only fire with an authoritative answer.

| Code | Violation | Spec | Response |
|---|---|---|---|
| T01 | a `[main-thread]` function called off the main thread | thread-check.h | Tolerate (+ debug assert when authoritative) |
| T02 | an `[audio-thread]` function called off the audio thread | thread-check.h | Tolerate (+ debug assert when authoritative) |
| T03 | `params.flush()` on the wrong thread for the current state | params.h: `[active ? audio-thread : main-thread]` | Tolerate |
| T04 | `tail.get()` on a thread that is neither main nor audio | tail.h: `[main-thread, audio-thread]` | Tolerate |

## G / X — Per-extension sequence contracts

| Code | Violation | Spec | Response |
|---|---|---|---|
| G01 | any gui method before `gui.create()` (except `is_api_supported`/`get_preferred_api`) | gui.h: "create() must have been called prior to asking the size" + documented call order | Reject where a bool return exists |
| G02 | `gui.create()` while a gui already exists | gui.h call order | Reject (false) |
| G03 | `gui.destroy()` without a created gui | gui.h call order | Tolerate — defensive teardown |
| G04 | embedded-only method (`set_size`, `set_parent`, `can_resize`, …) on a floating window, or `set_transient`/`suggest_title` on an embedded one | gui.h: `[main-thread & !floating]` vs `[main-thread & floating]` | Reject (false) / Tolerate for void returns |
| AP01 | `audio_ports.count()/get()` scan while active | audio-ports.h: "The audio ports scan has to be done while the plugin is deactivated" | Tolerate |
| NP01 | `note_ports.count()/get()` scan while active | note-ports.h: same rule | Tolerate |
| AC01 | `audio_ports_config.select()` while active | audio-ports-config.h: `[main-thread & plugin-deactivated]` | Reject (false) |
| CA01 | `configurable_audio_ports.can_apply/apply_configuration()` while active | configurable-audio-ports.h: `[main-thread & !active]` | Reject (false) |
| LT01 | `latency.get()` while neither active nor being activated | latency.h: `[main-thread & (being-activated | active)]` | Tolerate — the cached latency is returned (hosts commonly do this; expect this code to be noisy) |
| VI01 | `voice_info.get()` while not active | voice-info.h: `[main-thread & active]` | Tolerate |
| R01 | `render.set()` with a mode other than realtime/offline. Note: render.h has **no** `[!active]` predicate — setting the mode while active is legal and not flagged | render.h | Reject (false) |
| PL01 | `preset_load.from_location()` with a FILE kind and null location, or a PLUGIN kind with a non-null location | preset-load.h + preset-discovery location kinds | Reject (false) for the unusable FILE case; Tolerate otherwise |
| SR01 | `surround.get_channel_map()` with `channel_map_capacity` smaller than the port's channel count | surround.h: "The channel map capacity must be greater or equal to the channel count" | Tolerate — truncated |

Informational (logged, never counted): `state.load()` while active is **not**
forbidden by state.h and is logged at INFO only.

## Undetectable from inside a plugin

Documented here so nobody wonders why there is no code for them:

- **Use-after-free** of the `clap_plugin`, extension, descriptor or host
  pointers after `destroy()`/`deinit()` — the memory is gone; only poisoned
  sentinels could catch this probabilistically, and a crashed validator
  reports nothing.
- **Host reading `clap_process` pointers after `process()` returns** — the
  plugin cannot observe host behavior after the call.
- **Host writing to read-only input buffers/event lists** — writes to the
  host's own arrays are invisible to the plugin.
- **Host not discarding output after `CLAP_PROCESS_ERROR`** — no callback
  confirms what the host did with the buffer.
- **`on_main_thread()` latency** after `request_callback()` — host.h
  explicitly disclaims timing guarantees, so lateness is not a provable
  violation.
- **GUI/user interaction inside `entry.init()`** — not observable by the
  plugin being loaded.
- `request_restart()`/`request_process()` being ignored — both are allowed
  to be delayed indefinitely, so non-response is suspicious but never
  provable.

## Querying from a test harness

```c
#include <cvp/violations.h>

const cvp_plugin_violations_t* v =
    plugin->get_extension(plugin, CVP_EXT_VIOLATIONS);
uint32_t total = v->total(plugin);
for (uint32_t i = 0; i < v->distinct(plugin); ++i) {
    cvp_violation_entry_t e;
    if (v->get(plugin, i, &e))
        printf("%s x%u: %s\n", e.code, e.count, e.last_message);
}
```

All functions are `[thread-safe]`. `clear()` resets the counters (useful to
scope a specific test phase). The extension is served by every flavor in the
suite, even before `init()`.
