//
// test_actions.cpp — Tests for action tools (open, reveal, copy_path).
//

#include "test_framework.hpp"
#include "../src/actions.hpp"

#include <windows.h>

using namespace actions;

TEST(actions_path_exists_valid) {
    // The current executable directory should exist
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    CHECK(path_exists(path));
}

TEST(actions_path_exists_invalid) {
    CHECK(!path_exists("Z:\\nonexistent\\path\\that\\should\\not\\exist.xyz"));
}

TEST(actions_path_exists_empty) {
    CHECK(!path_exists(""));
}

TEST(actions_open_nonexistent_fails) {
    auto result = open("Z:\\nonexistent\\file.xyz");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
    CHECK_STR_EQ(result.action, "open");
}

TEST(actions_open_empty_path_fails) {
    auto result = open("");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
}

TEST(actions_reveal_nonexistent_fails) {
    auto result = reveal("Z:\\nonexistent\\file.xyz");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
    CHECK_STR_EQ(result.action, "reveal");
}

TEST(actions_reveal_empty_path_fails) {
    auto result = reveal("");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
}

TEST(actions_copy_path_nonexistent_fails) {
    auto result = copy_path("Z:\\nonexistent\\file.xyz");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
    CHECK_STR_EQ(result.action, "copy_path");
}

TEST(actions_copy_path_empty_fails) {
    auto result = copy_path("");
    CHECK(!result.success);
    CHECK(!result.error_message.empty());
}

TEST(actions_copy_path_roundtrip) {
    // Create a temporary file to test with
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "tst", 0, temp_path);

    // Verify the file exists
    CHECK(path_exists(temp_path));

    // Copy path to clipboard
    auto result = copy_path(temp_path);
    CHECK(result.success);

    // Verify clipboard contents
    CHECK(OpenClipboard(nullptr));
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    CHECK(hData != nullptr);
    if (hData) {
        wchar_t* text = static_cast<wchar_t*>(GlobalLock(hData));
        if (text) {
            // Convert temp_path to wide for comparison
            int len = MultiByteToWideChar(CP_UTF8, 0, temp_path, -1, nullptr, 0);
            std::wstring expected(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, temp_path, -1, expected.data(), len);
            CHECK(wcscmp(text, expected.c_str()) == 0);
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();

    // Clean up
    DeleteFileA(temp_path);
}

TEST(actions_copy_path_preserves_action_name) {
    // Create a temporary file
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "tst", 0, temp_path);

    auto result = copy_path(temp_path);
    CHECK(result.success);
    CHECK_STR_EQ(result.action, "copy_path");
    CHECK_STR_EQ(result.path, temp_path);

    DeleteFileA(temp_path);
}
