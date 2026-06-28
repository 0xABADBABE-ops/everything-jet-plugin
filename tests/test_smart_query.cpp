//
// test_smart_query.cpp — Tests for the NL to Everything query builder.
//

#include "test_framework.hpp"
#include "../src/smart_query.hpp"

using namespace smart_query;

TEST(smart_query_empty_intent) {
    auto result = build("");
    CHECK(result.query.empty());
}

TEST(smart_query_pdfs) {
    auto result = build("find all pdfs");
    CHECK(result.has_file_type);
    CHECK(result.query.find("ext:pdf") != std::string::npos);
}

TEST(smart_query_images) {
    auto result = build("show me images");
    CHECK(result.has_file_type);
    CHECK(result.query.find("ext:png") != std::string::npos);
}

TEST(smart_query_large_size) {
    auto result = build("large files");
    CHECK(result.has_size_filter);
    CHECK(result.query.find("size:>10mb") != std::string::npos);
}

TEST(smart_query_huge_size) {
    auto result = build("huge videos");
    CHECK(result.has_size_filter);
    CHECK(result.query.find("size:>100mb") != std::string::npos);
}

TEST(smart_query_explicit_size) {
    auto result = build("files larger than 5mb");
    CHECK(result.has_size_filter);
    CHECK(result.query.find("size:>5mb") != std::string::npos);
}

TEST(smart_query_today) {
    auto result = build("files created today");
    CHECK(result.has_date_filter);
    CHECK(result.query.find("dm:today") != std::string::npos);
}

TEST(smart_query_last_week) {
    auto result = build("files from last week");
    CHECK(result.has_date_filter);
    CHECK(result.query.find("dm:last7days") != std::string::npos);
}

TEST(smart_query_recent) {
    auto result = build("recent documents");
    CHECK(result.has_date_filter);
    CHECK(result.query.find("dm:last1hours") != std::string::npos);
}

TEST(smart_query_exclusion) {
    auto result = build("pdfs not in temp");
    CHECK(result.has_exclusion);
    CHECK(result.query.find("ext:pdf") != std::string::npos);
    // The exclusion should negate something related to temp
    CHECK(result.query.find("!") != std::string::npos);
}

TEST(smart_query_complex_intent) {
    auto result = build("find large pdfs from last month");
    CHECK(result.has_file_type);
    CHECK(result.has_size_filter);
    CHECK(result.has_date_filter);
    CHECK(result.query.find("ext:pdf") != std::string::npos);
    CHECK(result.query.find("size:>10mb") != std::string::npos);
    CHECK(result.query.find("last30days") != std::string::npos);
}

TEST(smart_query_path_filter) {
    auto result = build("code in C:\\Projects");
    CHECK(result.has_path_filter);
    CHECK(result.query.find("path:\"C:\\Projects\"") != std::string::npos);
}

TEST(smart_query_name_term) {
    auto result = build("report");
    CHECK(!result.query.empty());
    CHECK(result.query.find("report") != std::string::npos);
}

TEST(smart_query_match_file_type_pdf) {
    CHECK_STR_EQ(match_file_type("pdf"), "ext:pdf");
    CHECK_STR_EQ(match_file_type("pdfs"), "ext:pdf");
}

TEST(smart_query_match_file_type_cpp) {
    CHECK_STR_EQ(match_file_type("cpp"), "ext:cpp");
}

TEST(smart_query_match_file_type_unknown) {
    CHECK(match_file_type("xyzqwerty").empty());
}

TEST(smart_query_match_size_large) {
    CHECK_STR_EQ(match_size_filter("large"), "size:>10mb");
}

TEST(smart_query_match_size_empty) {
    CHECK_STR_EQ(match_size_filter("empty"), "size:0");
}

TEST(smart_query_match_date_today) {
    CHECK_STR_EQ(match_date_filter("today"), "dm:today");
}

TEST(smart_query_match_date_yesterday) {
    CHECK_STR_EQ(match_date_filter("yesterday"), "dm:yesterday");
}

TEST(smart_query_extract_path_backslash) {
    CHECK(!extract_path("C:\\Users\\test").empty());
}

TEST(smart_query_extract_path_no_path) {
    CHECK(extract_path("hello").empty());
}

TEST(smart_query_tokens_populated) {
    auto result = build("large pdfs today");
    CHECK(!result.tokens.empty());
}
