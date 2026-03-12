#pragma once

/// @file dtc_panel.h
/// DTC (Diagnostic Trouble Codes) tab rendering.

#include "gui/gui_state.h"
#include "gui/controllers/capture_controller.h"
#include "obd/dtc_decoder.h"

#include <string>
#include <vector>

namespace canmatik {

/// Mutable state for the DTC panel (owned by GuiApp, passed by reference).
struct DtcPanelState {
    std::vector<DtcCode> stored_dtcs;
    std::vector<DtcCode> pending_dtcs;
    std::vector<DtcCode> manual_dtcs;  ///< Manually added DTC codes
    std::string status_message;
    char        manual_dtc_input[6] = {};  ///< e.g. "P0300"
    bool        reading   = false;
    bool        clearing  = false;
};

/// Render the DTC tab.
/// @param is_file_mode If true, show offline DTCs decoded from buffer instead of live-read buttons.
void render_dtc_panel(DtcPanelState& dtc_state, CaptureController& capture,
                      GuiState& gui_state, bool is_file_mode = false);

} // namespace canmatik
