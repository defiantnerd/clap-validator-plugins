#include "gui/transport_format.h"

#include <clap/clap.h>

#include <cstdio>

namespace cvp {

TransportLines formatTransport(const GuiModel::TransportInfo& info) {
    TransportLines lines;
    char buf[160];

    if (!info.seen) {
        lines.line1 = "transport: nothing received yet";
        lines.line2 = "(the host delivers it with process(); is audio running?)";
        return lines;
    }
    if (!info.provided) {
        lines.line1 = "transport: host passed NULL (free-running)";
        lines.line2 = "";
        return lines;
    }

    // Line 1: state | tempo | time signature
    std::string state;
    state += (info.flags & CLAP_TRANSPORT_IS_PLAYING) ? "PLAYING" : "stopped";
    if (info.flags & CLAP_TRANSPORT_IS_RECORDING)
        state += " REC";
    if (info.flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE)
        state += " LOOP";
    if (info.flags & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL)
        state += " PRE-ROLL";

    lines.line1 = "transport: " + state;
    if (info.flags & CLAP_TRANSPORT_HAS_TEMPO) {
        std::snprintf(buf, sizeof(buf), " | %.2f BPM", info.tempo);
        lines.line1 += buf;
    } else {
        lines.line1 += " | no tempo";
    }
    if (info.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) {
        std::snprintf(buf, sizeof(buf), " | %u/%u", info.tsigNum, info.tsigDenom);
        lines.line1 += buf;
    } else {
        lines.line1 += " | no tsig";
    }

    // Line 2: positions | bar | loop
    if (info.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
        std::snprintf(buf, sizeof(buf), "beat %.3f | bar %d (start %.1f)", info.songPosBeats,
                      info.barNumber, info.barStartBeats);
        lines.line2 += buf;
    } else {
        lines.line2 += "no beats timeline";
    }
    if (info.flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) {
        std::snprintf(buf, sizeof(buf), " | %.2f s", info.songPosSeconds);
        lines.line2 += buf;
    } else {
        lines.line2 += " | no seconds timeline";
    }
    if (info.flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE) {
        std::snprintf(buf, sizeof(buf), " | loop %.1f..%.1f", info.loopStartBeats,
                      info.loopEndBeats);
        lines.line2 += buf;
    }
    return lines;
}

} // namespace cvp
