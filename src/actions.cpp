//
// actions.cpp — System action tools: open, reveal, copy_path.
//

#include "actions.hpp"
#include "string_utils.hpp"

#include <cstring>
#include <string>
#include <windows.h>
#include <shlobj.h>

namespace actions {

// ============================================================
// Utilities
// ============================================================

bool path_exists(const std::string& path) {
    if (path.empty()) return false;
    std::wstring wide = str_utils::utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wide.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

static std::string get_shell_error_string(HINSTANCE hInst) {
    DWORD code = reinterpret_cast<DWORD_PTR>(hInst);
    if (code <= 32) {
        switch (code) {
            case 0:  return "Out of memory or resources";
            case 2:  return "File not found";
            case 3:  return "Path not found";
            case 5:  return "Access denied";
            case 8:  return "Not enough memory";
            case 26: return "Sharing violation";
            case 27: return "Association incomplete";
            case 28: return "DDE timeout";
            case 29: return "DDE failed";
            case 30: return "DDE busy";
            case 31: return "No application associated with this file type";
            case 32: return "DLL not found";
            default: return "ShellExecute error code " + std::to_string(code);
        }
    }
    return {};
}

// ============================================================
// RAII clipboard guard
// ============================================================

namespace {

class ClipboardGuard {
public:
    explicit ClipboardGuard(HWND owner) : opened_(false) {
        // Retry a few times — clipboard may be locked by another app
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (OpenClipboard(owner)) {
                opened_ = true;
                break;
            }
            Sleep(10);
        }
    }

    ~ClipboardGuard() {
        if (opened_) {
            CloseClipboard();
        }
    }

    bool is_open() const { return opened_; }

    ClipboardGuard(const ClipboardGuard&) = delete;
    ClipboardGuard& operator=(const ClipboardGuard&) = delete;

private:
    bool opened_;
};

} // anonymous namespace

// ============================================================
// open
// ============================================================

ActionResult open(const std::string& path) {
    ActionResult result;
    result.action = "open";
    result.path = path;

    if (path.empty()) {
        result.error_message = "Path is required";
        return result;
    }

    if (!path_exists(path)) {
        result.error_message = "Path does not exist: " + path;
        return result;
    }

    std::wstring wide = str_utils::utf8_to_wide(path);

    HINSTANCE hInst = ShellExecuteW(
        nullptr,           // hwnd
        L"open",           // verb
        wide.c_str(),      // file
        nullptr,           // parameters
        nullptr,           // directory
        SW_SHOWNORMAL      // show
    );

    // ShellExecuteW returns HINSTANCE; values <= 32 indicate error
    if (reinterpret_cast<DWORD_PTR>(hInst) <= 32) {
        result.error_message = get_shell_error_string(hInst);
        return result;
    }

    result.success = true;
    return result;
}

// ============================================================
// reveal
// ============================================================

ActionResult reveal(const std::string& path) {
    ActionResult result;
    result.action = "reveal";
    result.path = path;

    if (path.empty()) {
        result.error_message = "Path is required";
        return result;
    }

    if (!path_exists(path)) {
        result.error_message = "Path does not exist: " + path;
        return result;
    }

    // Build the /select,"path" argument for explorer.exe
    std::wstring wide_path = str_utils::utf8_to_wide(path);
    std::wstring params = L"/select,\"" + wide_path + L"\"";

    HINSTANCE hInst = ShellExecuteW(
        nullptr,               // hwnd
        nullptr,               // verb (use default = open)
        L"explorer.exe",       // file
        params.c_str(),        // parameters
        nullptr,               // directory
        SW_SHOWNORMAL          // show
    );

    if (reinterpret_cast<DWORD_PTR>(hInst) <= 32) {
        result.error_message = get_shell_error_string(hInst);
        return result;
    }

    result.success = true;
    return result;
}

// ============================================================
// copy_path
// ============================================================

ActionResult copy_path(const std::string& path) {
    ActionResult result;
    result.action = "copy_path";
    result.path = path;

    if (path.empty()) {
        result.error_message = "Path is required";
        return result;
    }

    if (!path_exists(path)) {
        result.error_message = "Path does not exist: " + path;
        return result;
    }

    std::wstring wide = str_utils::utf8_to_wide(path);
    size_t byte_size = (wide.size() + 1) * sizeof(wchar_t);

    // Allocate global memory for clipboard data
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byte_size);
    if (!hMem) {
        result.error_message = "Failed to allocate clipboard memory";
        return result;
    }

    {
        void* locked = GlobalLock(hMem);
        if (!locked) {
            GlobalFree(hMem);
            result.error_message = "Failed to lock clipboard memory";
            return result;
        }
        std::memcpy(locked, wide.c_str(), byte_size);
        GlobalUnlock(hMem);
    }

    // RAII clipboard guard ensures CloseClipboard is always called
    ClipboardGuard clip(nullptr);
    if (!clip.is_open()) {
        GlobalFree(hMem);
        result.error_message = "Failed to open clipboard (locked by another app?)";
        return result;
    }

    // Must call EmptyClipboard before SetClipboardData
    EmptyClipboard();

    HANDLE hData = SetClipboardData(CF_UNICODETEXT, hMem);
    if (!hData) {
        // SetClipboardData failed — we must free the memory ourselves
        GlobalFree(hMem);
        result.error_message = "Failed to set clipboard data (error " +
            std::to_string(GetLastError()) + ")";
        return result;
    }

    // After successful SetClipboardData, the system owns hMem — do NOT free it
    result.success = true;
    return result;
}

} // namespace actions
