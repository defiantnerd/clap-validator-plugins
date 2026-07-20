#pragma once

#include <clap/clap.h>

namespace cvp {

class LogBuffer;

// Process-global sink for findings that occur before/without a plugin
// instance: DSO entry contract violations (E01..E04) and preset-discovery
// provider misuse (PD01). Lines are also printed to stderr immediately; every
// plugin copies the ring into its own LogBuffer at init() so entry-level
// findings surface in each GUI log and host log too.
namespace earlylog {

void append(clap_log_severity severity, const char* message) noexcept;

// Copies (does not consume) all lines gathered so far — several instances
// each want them. Lines appended after an instance's init() only reach
// instances created later; documented limitation.
void copyInto(LogBuffer& sink) noexcept;

} // namespace earlylog

} // namespace cvp
