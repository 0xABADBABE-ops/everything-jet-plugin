//
// test_string_utils.cpp — Tests for UTF-8 <-> wide string conversion.
//

#include "test_framework.hpp"
#include "../src/string_utils.hpp"

using str_utils::utf8_to_wide;
using str_utils::wide_to_utf8;

TEST(str_ascii_roundtrip) {
    std::string original = "Hello, World!";
    auto wide = utf8_to_wide(original);
    auto back = wide_to_utf8(wide);
    CHECK_STR_EQ(back, original);
}

TEST(str_empty) {
    CHECK(utf8_to_wide("").empty());
    CHECK(wide_to_utf8(L"").empty());
}

TEST(str_unicode_basic) {
    // Greek letters: alpha beta gamma
    std::string utf8 = "\xCE\xB1\xCE\xB2\xCE\xB3";
    auto wide = utf8_to_wide(utf8);
    CHECK_EQ(wide.size(), 3u);
    CHECK_EQ(wide[0], L'\u03B1');
    CHECK_EQ(wide[1], L'\u03B2');
    CHECK_EQ(wide[2], L'\u03B3');
    auto back = wide_to_utf8(wide);
    CHECK_STR_EQ(back, utf8);
}

TEST(str_emoji_4byte) {
    // Grinning face emoji U+1F600
    std::string utf8 = "\xF0\x9F\x98\x80";
    auto wide = utf8_to_wide(utf8);
    CHECK_EQ(wide.size(), 2u); // Surrogate pair in UTF-16
    auto back = wide_to_utf8(wide);
    CHECK_STR_EQ(back, utf8);
}

TEST(str_mixed_content) {
    std::string utf8 = "Path/Caf\xC3\xA9/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt";
    auto wide = utf8_to_wide(utf8);
    CHECK(wide.size() > 5);
    auto back = wide_to_utf8(wide);
    CHECK_STR_EQ(back, utf8);
}

TEST(str_windows_path) {
    std::string path = "C:\\Users\\Test\\Documents\\file.txt";
    auto wide = utf8_to_wide(path);
    CHECK_EQ(wide.size(), path.size()); // ASCII = same length
    auto back = wide_to_utf8(wide);
    CHECK_STR_EQ(back, path);
}

TEST(str_null_bytes_preserved) {
    // UTF-8 strings with embedded nulls should be preserved
    // Note: std::string handles embedded nulls, but c_str() truncates.
    // The conversion functions use string_view with size.
    std::string input(5, '\0');
    input[0] = 'A';
    input[2] = 'B';
    input[4] = 'C';
    auto wide = utf8_to_wide(input);
    CHECK_EQ(wide.size(), 5u);
}
