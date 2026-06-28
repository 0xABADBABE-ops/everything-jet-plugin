#pragma once

//
// everything_wrapper.hpp — RAII C++ wrapper around the Everything SDK C API.
// Provides clean, type-safe search with structured result objects.
//

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace everything {

struct SearchOptions {
    std::string query;
    bool match_path       = false;
    bool match_case       = false;
    bool match_whole_word = false;
    bool regex            = false;
    int  max_results      = 100;
    int  offset           = 0;
    int  sort             = 1;  // EVERYTHING_SORT_NAME_ASCENDING
    bool include_size     = true;
    bool include_dates    = true;
};

struct DateTriple {
    std::string created;
    std::string modified;
    std::string accessed;
};

struct SearchResult {
    int index;
    std::string file_name;
    std::string path;
    std::string full_path;
    std::string extension;
    bool is_folder    = false;
    bool is_file      = false;
    std::optional<int64_t> size;
    DateTriple dates;
    uint32_t attributes = 0;
};

struct SearchResponse {
    bool success = false;
    int error_code = 0;
    std::string error_message;
    int total_results   = 0;
    int returned_results = 0;
    int offset           = 0;
    std::vector<SearchResult> results;
};

struct VersionInfo {
    bool available = false;
    int major = 0;
    int minor = 0;
    int revision = 0;
    int build = 0;
};

// Check if Everything IPC is reachable (sends a trivial query)
bool is_running();

// Get Everything version info
VersionInfo get_version();

// Execute a search with the given options
SearchResponse search(const SearchOptions& opts);

// Get the total result count for a query without fetching results
int get_result_count(const std::string& query);

} // namespace everything
