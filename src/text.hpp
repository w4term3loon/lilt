// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace ren {

// Return valid, single-line UTF-8. Whitespace and control characters become
// spaces, preventing transcription output from synthesizing Return or Tab.
std::string normalize_dictation(std::string_view text);

} // namespace ren
