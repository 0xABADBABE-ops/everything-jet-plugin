//
// test_everything.cpp — Integration tests for the Everything SDK wrapper.
// These require Everything.exe to be running.
//

#include "test_framework.hpp"
#include "../src/everything_wrapper.hpp"

using namespace everything;

TEST(everything_is_running) {
    bool running = is_running();
    if (!running) {
        std::fprintf(stderr, "  [WARN] Everything is not running — "
                     "integration tests will be limited\n");
    }
    // Don't fail the test suite if Everything isn't running,
    // but record the status
    CHECK(true); // always pass; just report
}

TEST(everything_get_version) {
    auto vi = get_version();
    if (!vi.available) {
        std::fprintf(stderr, "  [SKIP] Everything not available\n");
        return;
    }
    CHECK(vi.major >= 1);
    std::printf("  Everything version: %d.%d.%d.%d\n",
                vi.major, vi.minor, vi.revision, vi.build);
}

TEST(everything_basic_search) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "notepad.exe";
    opts.max_results = 10;

    auto resp = search(opts);
    CHECK(resp.success);
    CHECK(resp.returned_results > 0);

    bool found_system32 = false;
    for (const auto& r : resp.results) {
        CHECK(!r.full_path.empty());
        CHECK(!r.file_name.empty());
        if (r.full_path.find("System32") != std::string::npos ||
            r.full_path.find("system32") != std::string::npos) {
            found_system32 = true;
        }
    }
    CHECK(found_system32);
}

TEST(everything_search_with_wildcard) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "*.txt";
    opts.max_results = 5;

    auto resp = search(opts);
    CHECK(resp.success);
    CHECK(resp.results.size() <= 5);

    for (const auto& r : resp.results) {
        // Each result should end with .txt or be a path containing .txt
        if (!r.extension.empty()) {
            CHECK_STR_EQ(r.extension, "txt");
        }
    }
}

TEST(everything_search_match_path) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "Windows";
    opts.match_path = true;
    opts.max_results = 5;

    auto resp = search(opts);
    CHECK(resp.success);
    // Results should contain "Windows" in their paths
    if (!resp.results.empty()) {
        bool has_windows = false;
        for (const auto& r : resp.results) {
            if (r.full_path.find("Windows") != std::string::npos ||
                r.path.find("Windows") != std::string::npos) {
                has_windows = true;
                break;
            }
        }
        CHECK(has_windows);
    }
}

TEST(everything_search_pagination) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts1;
    opts1.query = "*.exe";
    opts1.max_results = 3;
    opts1.offset = 0;

    SearchOptions opts2;
    opts2.query = "*.exe";
    opts2.max_results = 3;
    opts2.offset = 1;

    auto resp1 = search(opts1);
    auto resp2 = search(opts2);

    CHECK(resp1.success);
    CHECK(resp2.success);

    if (resp1.results.size() >= 3 && resp2.results.size() >= 2) {
        // Second query offset by 1 should skip the first result
        CHECK_STR_EQ(resp1.results[1].full_path, resp2.results[0].full_path);
    }
}

TEST(everything_search_total_count) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "*.dll";
    opts.max_results = 10;

    auto resp = search(opts);
    CHECK(resp.success);
    // There should be many DLLs on any Windows system
    CHECK(resp.total_results > 100);
    // But we only fetched 10
    CHECK_EQ(resp.returned_results, 10);
    CHECK_EQ(static_cast<int>(resp.results.size()), 10);
}

TEST(everything_get_count_function) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    int count = get_result_count("notepad.exe");
    CHECK(count > 0);
}

TEST(everything_search_includes_size_and_dates) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "notepad.exe";
    opts.include_size = true;
    opts.include_dates = true;
    opts.max_results = 5;

    auto resp = search(opts);
    CHECK(resp.success);

    for (const auto& r : resp.results) {
        if (r.is_file) {
            CHECK(r.size.has_value());
        }
        if (!r.dates.modified.empty()) {
            // Should be ISO 8601 format: YYYY-MM-DDThh:mm:ssZ
            CHECK(r.dates.modified.size() >= 19);
        }
    }
}

TEST(everything_search_with_sort) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "*.log";
    opts.sort = 6; // EVERYTHING_SORT_SIZE_DESCENDING
    opts.max_results = 5;
    opts.include_size = true;

    auto resp = search(opts);
    CHECK(resp.success);

    // Results should be sorted by size descending
    if (resp.results.size() >= 2) {
        for (size_t i = 1; i < resp.results.size(); ++i) {
            int64_t prev_size = resp.results[i-1].size.value_or(0);
            int64_t curr_size = resp.results[i].size.value_or(0);
            CHECK(prev_size >= curr_size);
        }
    }
}

TEST(everything_empty_query) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "";
    opts.max_results = 5;

    auto resp = search(opts);
    CHECK(resp.success);
    // Empty query returns everything (up to max_results)
    CHECK_EQ(resp.returned_results, 5);
}

TEST(everything_nonexistent_file) {
    if (!is_running()) {
        std::fprintf(stderr, "  [SKIP] Everything not running\n");
        return;
    }

    SearchOptions opts;
    opts.query = "ZZZZZZ_nonexistent_file_xyz.ZZZ";
    opts.max_results = 10;

    auto resp = search(opts);
    CHECK(resp.success);
    CHECK_EQ(resp.returned_results, 0);
    CHECK(resp.results.empty());
}
