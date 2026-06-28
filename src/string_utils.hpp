#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace str_utils {

inline std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

inline std::string wide_to_utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                        static_cast<int>(wide.size()),
                        utf8.data(), len, nullptr, nullptr);
    return utf8;
}

} // namespace str_utils
