#pragma once

//
// content_search.hpp — Search inside file contents and extract matching snippets.
// Uses Everything SDK to find candidate files, then scans each file for the
// search pattern, returning structured results with surrounding context lines.
//

#include <optional>
#include <string>
#include <vector>

namespace content_search {

struct ContentMatch {
    std::string file_path;
    int line_number = 0;
    std::string match_line;
    std::vector<std::string> context_before;
    std::vector<std::string> context_after;
};

struct SearchOptions {
    std::string pattern;                    // text or regex to find
    std::string path_filter;                // optional path scope
    std::vector<std::string> extensions;    // empty = default text extensions
    bool match_case = false;
    bool regex = false;
    int max_files = 200;                    // max files to scan
    int max_matches_per_file = 5;
    int context_lines = 2;                  // lines before/after each match
    int max_results = 50;                   // max total matches to return
};

struct SearchResponse {
    bool success = false;
    std::string error_message;
    int files_searched = 0;
    int total_matches = 0;
    std::vector<ContentMatch> matches;
};

// Full content search: uses Everything to find candidate text files, then
// scans each one for the pattern and extracts snippets.
SearchResponse search(const SearchOptions& opts);

// --- Utilities (exposed for testing) ---

// Check if a file extension is a known text/code type
bool is_text_extension(const std::string& ext);

// Read a file as UTF-8, detecting BOM for UTF-8/UTF-16. Returns nullopt
// if the file cannot be opened or exceeds max_size.
std::optional<std::string> read_text_file(const std::string& path,
                                          size_t max_size = 10 * 1024 * 1024);

// Extract matches from file content. Splits into lines, searches for the
// pattern (substring or regex), and collects context lines around each hit.
std::vector<ContentMatch> extract_matches(
    const std::string& content,
    const std::string& file_path,
    const std::string& pattern,
    bool match_case,
    bool use_regex,
    int context_lines,
    int max_matches);

// Default text extensions used when none are specified
const std::vector<std::string>& default_text_extensions();

} // namespace content_search
