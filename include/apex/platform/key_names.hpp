#pragma once

#include <string>

namespace apex {

// What a main-block key (a letter, digit or punctuation key) prints on the player's keyboard layout, as one
// upper-case character in UTF-8: GLFW key 'Z' gives "Y" on a QWERTZ keyboard. Empty when the key is not one of
// those, or the layout has no name for it, or GLFW has not been started (as in the unit tests).
[[nodiscard]] std::string layoutKeyName(int glfwKey);

}  // namespace apex
