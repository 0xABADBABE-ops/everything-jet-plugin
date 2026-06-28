//
// content_search.cpp — Implementation of content search with snippet extraction.
//

#include "content_search.hpp"
#include "everything_wrapper.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <windows.h>

namespace content_search {

// ============================================================
// Default text extensions
// ============================================================

const std::vector<std::string>& default_text_extensions() {
    static const std::vector<std::string> exts = {
        "txt", "log", "md", "rst", "adoc",
        // C/C++
        "c", "h", "cpp", "hpp", "cxx", "hxx", "cc", "inl", "ipp",
        // C#
        "cs",
        // Java
        "java", "kt", "kts", "gradle",
        // Web
        "js", "jsx", "ts", "tsx", "php", "vue", "svelte",
        "html", "htm", "css", "scss", "sass", "less",
        // Scripts
        "py", "rb", "go", "rs", "swift", "dart", "lua",
        "sh", "bash", "zsh", "fish", "bat", "cmd", "ps1", "psm1",
        // Data
        "json", "yaml", "yml", "toml", "xml", "csv", "tsv",
        "ini", "cfg", "conf", "properties", "env",
        // Build
        "cmake", "make", "mk",
        // Other
        "sql", "graphql", "gql", "proto", "dockerfile",
        "gitignore", "gitattributes", "editorconfig",
        "vim", "el", "clj", "ex", "exs", "erl", "hs", "scala",
        "r", "jl", "pl", "pm", "pas", "asm", "s",
    };
    return exts;
}

bool is_text_extension(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    // Strip leading dot
    if (!lower.empty() && lower[0] == '.') lower.erase(0, 1);
    const auto& exts = default_text_extensions();
    return std::find(exts.begin(), exts.end(), lower) != exts.end();
}

// ============================================================
// File reading with encoding detection
// ============================================================

std::optional<std::string> read_text_file(const std::string& path,
                                          size_t max_size) {
    // Open in binary mode to control encoding ourselves
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    auto size = file.tellg();
    if (size < 0) return std::nullopt;
    size_t file_size = static_cast<size_t>(size);
    if (file_size > max_size) return std::nullopt;
    if (file_size == 0) return std::string{};

    file.seekg(0, std::ios::beg);
    std::string raw(file_size, '\0');
    if (!file.read(raw.data(), file_size)) return std::nullopt;

    // Detect BOM
    if (file_size >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        // UTF-8 BOM — strip it
        return raw.substr(3);
    }

    if (file_size >= 2) {
        // UTF-16 LE BOM: FF FE
        if (static_cast<unsigned char>(raw[0]) == 0xFF &&
            static_cast<unsigned char>(raw[1]) == 0xFE) {
            std::wstring wide((file_size - 2) / 2, L'\0');
            std::memcpy(wide.data(), raw.data() + 2, file_size - 2);
            return str_utils::wide_to_utf8(wide);
        }
        // UTF-16 BE BOM: FE FF — byte-swap then convert
        if (static_cast<unsigned char>(raw[0]) == 0xFE &&
            static_cast<unsigned char>(raw[1]) == 0xFF) {
            std::wstring wide((file_size - 2) / 2, L'\0');
            std::memcpy(wide.data(), raw.data() + 2, file_size - 2);
            // Swap bytes
            for (auto& c : wide) c = (c >> 8) | (c << 8);
            return str_utils::wide_to_utf8(wide);
        }
    }

    // No BOM — assume UTF-8 (ASCII is a subset)
    return raw;
}

// ============================================================
// Line splitting
// ============================================================

static std::vector<std::string> split_lines(const std::string& content) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            size_t end = i;
            if (end > start && content[end - 1] == '\r') --end;
            lines.emplace_back(content.substr(start, end - start));
            start = i + 1;
        }
    }
    if (start < content.size()) {
        size_t end = content.size();
        if (end > start && content[end - 1] == '\r') --end;
        lines.emplace_back(content.substr(start, end - start));
    }
    return lines;
}

// ============================================================
// Case-insensitive substring search
// ============================================================

static std::string to_lower(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return result;
}

// ============================================================
// Snippet extraction
// ============================================================

std::vector<ContentMatch> extract_matches(
    const std::string& content,
    const std::string& file_path,
    const std::string& pattern,
    bool match_case,
    bool use_regex,
    int context_lines,
    int max_matches) {

    std::vector<ContentMatch> matches;
    if (pattern.empty() || content.empty()) return matches;

    auto lines = split_lines(content);

    // Prepare search tools
    std::string lower_pattern;
    std::regex re;
    bool regex_valid = false;

    if (use_regex) {
        auto flags = std::regex::ECMAScript;
        if (!match_case) flags |= std::regex::icase;
        try {
            re = std::regex(pattern, flags);
            regex_valid = true;
        } catch (...) {
            return matches; // invalid regex
        }
    } else if (!match_case) {
        lower_pattern = to_lower(pattern);
    }

    for (int i = 0; i < static_cast<int>(lines.size()) &&
                     static_cast<int>(matches.size()) < max_matches; ++i) {
        const std::string& line = lines[i];
        bool found = false;

        if (use_regex) {
            found = std::regex_search(line, re);
        } else if (match_case) {
            found = line.find(pattern) != std::string::npos;
        } else {
            found = to_lower(line).find(lower_pattern) != std::string::npos;
        }

        if (!found) continue;

        ContentMatch m;
        m.file_path = file_path;
        m.line_number = i + 1; // 1-indexed
        m.match_line = line;

        // Extract context before
        int ctx_start = (std::max)(0, i - context_lines);
        for (int j = ctx_start; j < i; ++j) {
            m.context_before.push_back(lines[j]);
        }

        // Extract context after
        int ctx_end = (std::min)(static_cast<int>(lines.size()) - 1, i + context_lines);
        for (int j = i + 1; j <= ctx_end; ++j) {
            m.context_after.push_back(lines[j]);
        }

        matches.push_back(std::move(m));
    }

    return matches;
}

// ============================================================
// Full content search — find files via Everything, then scan
// ============================================================

SearchResponse search(const SearchOptions& opts) {
    SearchResponse resp;

    if (opts.pattern.empty()) {
        resp.error_message = "Pattern is required";
        return resp;
    }

    // Determine which extensions to search
    const auto& search_exts = opts.extensions.empty()
        ? default_text_extensions()
        : opts.extensions;

    // Build Everything query to find candidate text files
    std::string query;

    // Add path filter
    if (!opts.path_filter.empty()) {
        query += "path:\"" + opts.path_filter + "\" ";
    }

    // Add extension filter — Everything uses OR for multiple ext: terms
    // Group as: ext:cpp;h;hpp;txt  (semicolon-separated in a single ext:)
    // Everything uses semicolons for multiple extensions: ext:cpp;hpp;txt
    if (search_exts.size() == 1) {
        query += "ext:" + search_exts[0];
    } else if (search_exts.size() <= 20) {
        query += "ext:";
        for (size_t i = 0; i < search_exts.size(); ++i) {
            if (i > 0) query += ";";
            query += search_exts[i];
        }
    }
    // If too many extensions, don't filter (will rely on is_text_extension check)

    // Search Everything for candidate files
    everything::SearchOptions es_opts;
    es_opts.query = query;
    es_opts.max_results = opts.max_files;
    es_opts.include_size = true;
    es_opts.include_dates = false;

    auto es_resp = everything::search(es_opts);
    if (!es_resp.success) {
        resp.error_message = es_resp.error_message;
        return resp;
    }

    // Build a set of allowed extensions for filtering
    std::unordered_set<std::string> ext_set;
    for (const auto& e : search_exts) {
        std::string lower = e;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (!lower.empty() && lower[0] == '.') lower.erase(0, 1);
        ext_set.insert(lower);
    }

    // Scan each file
    for (const auto& r : es_resp.results) {
        // Double-check extension (Everything might return non-text files)
        if (!ext_set.empty()) {
            std::string ext = r.extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext_set.find(ext) == ext_set.end()) continue;
        }

        // Read and scan
        auto content = read_text_file(r.full_path);
        if (!content) continue;

        resp.files_searched++;

        auto file_matches = extract_matches(
            *content, r.full_path, opts.pattern,
            opts.match_case, opts.regex,
            opts.context_lines, opts.max_matches_per_file);

        for (auto& m : file_matches) {
            if (static_cast<int>(resp.matches.size()) >= opts.max_results) break;
            resp.matches.push_back(std::move(m));
            resp.total_matches++;
        }

        if (static_cast<int>(resp.matches.size()) >= opts.max_results) break;
    }

    resp.success = true;
    return resp;
}

} // namespace content_search
