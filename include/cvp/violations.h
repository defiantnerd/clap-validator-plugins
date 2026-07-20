/* Machine-readable host-contract violation counters.
 *
 * Every plugin in the clap-validator-plugins suite exposes this custom
 * extension so a test harness can programmatically read what the plugin
 * observed, instead of scraping log output:
 *
 *     const cvp_plugin_violations_t* v =
 *         plugin->get_extension(plugin, CVP_EXT_VIOLATIONS);
 *
 * Codes ("L03", "P02", ...) are stable identifiers documented in
 * docs/host-contract-violations.md. All functions are [thread-safe].
 */
#pragma once

#include <clap/clap.h>

#define CVP_EXT_VIOLATIONS "org.clap-validator.violations/1"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cvp_violation_entry {
    char code[8];           /* stable violation code, e.g. "L03" */
    uint32_t count;         /* occurrences observed on this instance */
    char last_message[256]; /* most recent full log line for this code */
} cvp_violation_entry_t;

typedef struct cvp_plugin_violations {
    /* Total violations observed on this instance. [thread-safe] */
    uint32_t(CLAP_ABI* total)(const clap_plugin_t* plugin);

    /* Number of distinct codes observed (= valid indices for get). [thread-safe] */
    uint32_t(CLAP_ABI* distinct)(const clap_plugin_t* plugin);

    /* Fetches the entry at index in [0, distinct()); returns false when out
     * of range. Entries are ordered by code. [thread-safe] */
    bool(CLAP_ABI* get)(const clap_plugin_t* plugin, uint32_t index,
                        cvp_violation_entry_t* entry);

    /* Resets all counters and messages. [thread-safe] */
    void(CLAP_ABI* clear)(const clap_plugin_t* plugin);
} cvp_plugin_violations_t;

#ifdef __cplusplus
}
#endif
