// Opens the workbench UI for the user (presentation concern, no core logic).
#pragma once

#include <string>

namespace wb::api {

/// Windows: tries Microsoft Edge in app mode (chromeless window, present on
/// every Windows 11 install), then the default browser. Elsewhere: xdg-open.
/// Returns a human-readable description of what was attempted.
std::string openUserInterface(const std::string& url);

}  // namespace wb::api
