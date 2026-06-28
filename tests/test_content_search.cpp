//
// test_content_search.cpp — Tests for content search and snippet extraction.
//

#include "test_framework.hpp"
#include "../src/content_search.hpp"

using namespace content_search;

// ============================================================
// is_text_extension
// ============================================================

TEST(content_ext_recognizes_cpp) {
    CHECK(is_text_extension("cpp"));
    CHECK(is_text_extension(".cpp"));
    CHECK(is_text_extension("CPP"));
}

TEST(content_ext_recognizes_common_types) {
    CHECK(is_text_extension("py"));
    CHECK(is_text_extension("json"));
    CHECK(is_text_extension("md"));
    CHECK(is_text_extension("txt"));
    CHECK(is_text_extension("h"));
    CHECK(is_text_extension("ts"));
    CHECK(is_text_extension("rs"));
    CHECK(is_text_extension("lua"));
}

TEST(content_ext_rejects_binary) {
    CHECK(!is_text_extension("exe"));
    CHECK(!is_text_extension("dll"));
    CHECK(!is_text_extension("png"));
    CHECK(!is_text_extension("zip"));
    CHECK(!is_text_extension("pdf"));
}

TEST(content_ext_empty_is_false) {
    CHECK(!is_text_extension(""));
}

// ============================================================
// extract_matches — substring
// ============================================================

TEST(content_extract_simple_match) {
    std::string content = "line one\nhello world\nline three";
    auto matches = extract_matches(content, "test.txt", "hello", false, false, 1, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK_STR_EQ(matches[0].file_path, "test.txt");
    CHECK_EQ(matches[0].line_number, 2);
    CHECK_STR_EQ(matches[0].match_line, "hello world");
}

TEST(content_extract_case_insensitive) {
    std::string content = "Hello World\nHELLO there\nsay hello";
    auto matches = extract_matches(content, "f.txt", "hello", false, false, 0, 10);

    CHECK_EQ(matches.size(), 3u);
}

TEST(content_extract_case_sensitive) {
    std::string content = "Hello World\nHELLO there\nsay hello";
    auto matches = extract_matches(content, "f.txt", "Hello", true, false, 0, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK_EQ(matches[0].line_number, 1);
}

TEST(content_extract_context_lines) {
    std::string content = "a\nb\nMATCH\nc\nd\ne";
    auto matches = extract_matches(content, "f.txt", "MATCH", false, false, 2, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK_EQ(matches[0].context_before.size(), 2u);
    CHECK_STR_EQ(matches[0].context_before[0], "a");
    CHECK_STR_EQ(matches[0].context_before[1], "b");
    CHECK_EQ(matches[0].context_after.size(), 2u);
    CHECK_STR_EQ(matches[0].context_after[0], "c");
    CHECK_STR_EQ(matches[0].context_after[1], "d");
}

TEST(content_extract_context_at_start) {
    std::string content = "MATCH\nb\nc";
    auto matches = extract_matches(content, "f.txt", "MATCH", false, false, 5, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK(matches[0].context_before.empty());
    CHECK_EQ(matches[0].context_after.size(), 2u);
}

TEST(content_extract_context_at_end) {
    std::string content = "a\nb\nMATCH";
    auto matches = extract_matches(content, "f.txt", "MATCH", false, false, 5, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK_EQ(matches[0].context_before.size(), 2u);
    CHECK(matches[0].context_after.empty());
}

TEST(content_extract_multiple_matches) {
    std::string content = "foo\nbar\nfoo\nbaz\nfoo";
    auto matches = extract_matches(content, "f.txt", "foo", false, false, 0, 10);

    CHECK_EQ(matches.size(), 3u);
    CHECK_EQ(matches[0].line_number, 1);
    CHECK_EQ(matches[1].line_number, 3);
    CHECK_EQ(matches[2].line_number, 5);
}

TEST(content_extract_max_matches_limit) {
    std::string content = "match\nmatch\nmatch\nmatch\nmatch";
    auto matches = extract_matches(content, "f.txt", "match", false, false, 0, 2);

    CHECK_EQ(matches.size(), 2u);
}

TEST(content_extract_no_matches) {
    std::string content = "nothing here\nmove along";
    auto matches = extract_matches(content, "f.txt", "xyz", false, false, 2, 10);

    CHECK(matches.empty());
}

TEST(content_extract_empty_content) {
    auto matches = extract_matches("", "f.txt", "x", false, false, 2, 10);
    CHECK(matches.empty());
}

TEST(content_extract_empty_pattern) {
    auto matches = extract_matches("content", "f.txt", "", false, false, 2, 10);
    CHECK(matches.empty());
}

// ============================================================
// extract_matches — regex
// ============================================================

TEST(content_extract_regex_simple) {
    std::string content = "foo123bar\nbaz456qux\nnope";
    auto matches = extract_matches(content, "f.txt", R"(\d+)", false, true, 0, 10);

    CHECK_EQ(matches.size(), 2u);
    CHECK_EQ(matches[0].line_number, 1);
    CHECK_EQ(matches[1].line_number, 2);
}

TEST(content_extract_regex_case_insensitive) {
    std::string content = "Hello\nhello\nHELLO";
    auto matches = extract_matches(content, "f.txt", "hello", false, true, 0, 10);

    CHECK_EQ(matches.size(), 3u);
}

TEST(content_extract_regex_case_sensitive) {
    std::string content = "Hello\nhello\nHELLO";
    auto matches = extract_matches(content, "f.txt", "Hello", true, true, 0, 10);

    CHECK_EQ(matches.size(), 1u);
}

TEST(content_extract_regex_invalid_returns_empty) {
    std::string content = "test";
    auto matches = extract_matches(content, "f.txt", "[invalid", false, true, 0, 10);
    CHECK(matches.empty());
}

// ============================================================
// read_text_file
// ============================================================

TEST(content_read_nonexistent_returns_nullopt) {
    auto result = read_text_file("zzz_nonexistent_file_xyz.zzz");
    CHECK(!result.has_value());
}

TEST(content_read_existing_file) {
    // Read this test file itself
    auto result = read_text_file(__FILE__);
    CHECK(result.has_value());
    CHECK(result->size() > 0);
    // Should contain "TEST(content_read_existing_file)"
    CHECK(result->find("content_read_existing_file") != std::string::npos);
}

// ============================================================
// CRLF handling
// ============================================================

TEST(content_extract_handles_crlf) {
    std::string content = "a\r\nMATCH\r\nb";
    auto matches = extract_matches(content, "f.txt", "MATCH", false, false, 1, 10);

    CHECK_EQ(matches.size(), 1u);
    CHECK_STR_EQ(matches[0].match_line, "MATCH");
    CHECK_EQ(matches[0].context_before.size(), 1u);
    CHECK_STR_EQ(matches[0].context_before[0], "a");
}
