#pragma once

//
// smart_query.hpp — Natural language to Everything search syntax translator.
// Tokenizes an intent string and assembles a structured Everything query.
//

#include <string>
#include <vector>

namespace smart_query {

struct BuildResult {
    std::string query;                  // assembled Everything query string
    std::vector<std::string> tokens;    // recognized tokens for debugging
    bool has_file_type   = false;
    bool has_size_filter = false;
    bool has_date_filter = false;
    bool has_path_filter = false;
    bool has_exclusion   = false;
};

// Parse a natural-language intent and build an Everything query string.
BuildResult build(const std::string& intent);

// --- Utilities exposed for testing ---

// Check if a word maps to a known file-type category.
// Returns the Everything ext: syntax, or empty string if unrecognized.
std::string match_file_type(const std::string& word);

// Check if a word/phrase maps to a size filter.
// Returns the Everything size: syntax, or empty string if unrecognized.
std::string match_size_filter(const std::string& word);

// Check if a word/phrase maps to a date filter.
// Returns the Everything dm: syntax, or empty string if unrecognized.
std::string match_date_filter(const std::string& word);

// Extract a path-like token (contains backslash or drive letter).
// Returns the path or empty string.
std::string extract_path(const std::string& word);

} // namespace smart_query
