#pragma once

#include <string>

namespace falcon {

// The system clipboard as UTF-8 text: what is in it (empty when there is no text or it cannot be opened) and putting text there.
[[nodiscard]] std::string clipboardText();
void setClipboardText(const std::string& utf8);

}  // namespace falcon
