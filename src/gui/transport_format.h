#pragma once

#include <string>

#include "gui/gui_model.h"

namespace cvp {

// Formats a transport snapshot into the two lines shown at the top of every
// platform view — shared so all platforms render identically.
//   line 1: state flags, tempo, time signature
//   line 2: song position (beats/seconds), bar, loop range
struct TransportLines {
    std::string line1;
    std::string line2;
};

TransportLines formatTransport(const GuiModel::TransportInfo& info);

} // namespace cvp
