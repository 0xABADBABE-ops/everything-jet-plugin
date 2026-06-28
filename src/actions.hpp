#pragma once

//
// actions.hpp — System action tools for file interaction.
// open, reveal, copy_path — all validate the path exists before acting.
//

#include <string>

namespace actions {

struct ActionResult {
    bool success = false;
    std::string action;       // "open", "reveal", "copy_path"
    std::string path;
    std::string error_message;
};

// Open a file or folder with its default application (ShellExecute "open").
// Validates path exists before attempting.
ActionResult open(const std::string& path);

// Reveal a file or folder in Windows Explorer with the item selected.
// Uses explorer.exe /select,"path".
ActionResult reveal(const std::string& path);

// Copy the absolute file path to the clipboard.
// Uses OpenClipboard + SetClipboardData(CF_UNICODETEXT).
ActionResult copy_path(const std::string& path);

// --- Utilities for testing ---

// Check if a path exists (file or directory).
bool path_exists(const std::string& path);

} // namespace actions
