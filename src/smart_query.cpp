//
// smart_query.cpp — Natural language to Everything search syntax translator.
//

#include "smart_query.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace smart_query {

// ============================================================
// Helpers
// ============================================================

static std::string to_lower(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return result;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string current;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) words.push_back(current);
    return words;
}

// ============================================================
// File-type matching
// ============================================================

static const std::map<std::string, std::string>& file_type_map() {
    static const std::map<std::string, std::string> types = {
        // Single extensions
        {"pdf",       "ext:pdf"},
        {"pdfs",      "ext:pdf"},
        {"doc",       "ext:doc;docx"},
        {"docs",      "ext:doc;docx"},
        {"document",  "ext:doc;docx;pdf;txt;rtf;odt"},
        {"documents", "ext:doc;docx;pdf;txt;rtf;odt"},
        {"xls",       "ext:xls;xlsx"},
        {"excel",     "ext:xls;xlsx"},
        {"ppt",       "ext:ppt;pptx"},
        {"powerpoint","ext:ppt;pptx"},

        // Image categories
        {"image",     "ext:png;jpg;jpeg;gif;bmp;webp;svg;ico;tiff;tif"},
        {"images",    "ext:png;jpg;jpeg;gif;bmp;webp;svg;ico;tiff;tif"},
        {"picture",   "ext:png;jpg;jpeg;gif;bmp;webp;svg;ico;tiff;tif"},
        {"pictures",  "ext:png;jpg;jpeg;gif;bmp;webp;svg;ico;tiff;tif"},
        {"photo",     "ext:jpg;jpeg;png;heic;raw;cr2;nef;arw"},
        {"photos",    "ext:jpg;jpeg;png;heic;raw;cr2;nef;arw"},

        // Video categories
        {"video",     "ext:mp4;avi;mkv;mov;wmv;flv;webm;m4v;mpg;mpeg"},
        {"videos",    "ext:mp4;avi;mkv;mov;wmv;flv;webm;m4v;mpg;mpeg"},
        {"movie",     "ext:mp4;avi;mkv;mov;wmv;flv;webm;m4v;mpg;mpeg"},
        {"movies",    "ext:mp4;avi;mkv;mov;wmv;flv;webm;m4v;mpg;mpeg"},

        // Audio categories
        {"audio",     "ext:mp3;wav;flac;aac;ogg;wma;m4a"},
        {"music",     "ext:mp3;flac;aac;ogg;wma;m4a;wav"},
        {"song",      "ext:mp3;flac;aac;ogg;wma;m4a;wav"},
        {"songs",     "ext:mp3;flac;aac;ogg;wma;m4a;wav"},

        // Archive categories
        {"archive",   "ext:zip;rar;7z;tar;gz;bz2;xz"},
        {"archives",  "ext:zip;rar;7z;tar;gz;bz2;xz"},
        {"zip",       "ext:zip"},
        {"zips",      "ext:zip"},

        // Code categories
        {"code",      "ext:cpp;c;h;hpp;py;js;ts;jsx;tsx;java;go;rs;rb;cs;swift;kt;lua;sh;bat;ps1"},
        {"source",    "ext:cpp;c;h;hpp;py;js;ts;jsx;tsx;java;go;rs;rb;cs;swift;kt;lua;sh;bat;ps1"},
        {"script",    "ext:py;sh;bat;ps1;js;rb;lua;pl"},
        {"scripts",   "ext:py;sh;bat;ps1;js;rb;lua;pl"},

        // Config / data
        {"config",    "ext:json;yaml;yml;toml;ini;cfg;conf;xml;env"},
        {"json",      "ext:json"},
        {"xml",       "ext:xml"},
        {"yaml",      "ext:yaml;yml"},

        // Executables
        {"exe",       "ext:exe"},
        {"executable","ext:exe"},
        {"dll",       "ext:dll"},
        {"library",   "ext:dll;lib;so"},

        // Text
        {"text",      "ext:txt;md;rst;log;csv;tsv"},
        {"log",       "ext:log"},
        {"logs",      "ext:log"},
        {"markdown",  "ext:md;markdown"},
        {"readme",    "readme"},

        // Fonts
        {"font",      "ext:ttf;otf;woff;woff2"},
        {"fonts",     "ext:ttf;otf;woff;woff2"},
    };
    return types;
}

std::string match_file_type(const std::string& word) {
    std::string lower = to_lower(word);
    const auto& types = file_type_map();
    auto it = types.find(lower);
    if (it != types.end()) return it->second;

    // Check if it looks like a bare extension (e.g., "cpp", "py")
    if (lower.size() <= 6 && std::all_of(lower.begin(), lower.end(),
            [](unsigned char c) { return std::isalnum(c); })) {
        // Validate against a broad set of known extensions
        static const std::vector<std::string> known_exts = {
            "cpp","c","h","hpp","py","js","ts","jsx","tsx","java","go","rs",
            "rb","cs","swift","kt","lua","sh","bat","ps1","txt","md","json",
            "xml","yaml","yml","toml","ini","cfg","pdf","doc","docx","xls",
            "xlsx","ppt","pptx","mp3","mp4","avi","mkv","zip","rar","7z",
            "png","jpg","jpeg","gif","bmp","svg","ico","exe","dll","msi",
            "iso","tar","gz","wav","flac","aac","ogg","wma","m4a","webm",
            "sql","csv","tsv","log","rtf","odt","HEIC","heic","raw"
        };
        for (const auto& e : known_exts) {
            if (lower == e) return "ext:" + lower;
        }
    }

    return {};
}

// ============================================================
// Size matching
// ============================================================

std::string match_size_filter(const std::string& word) {
    std::string lower = to_lower(word);

    if (lower == "large" || lower == "big")    return "size:>10mb";
    if (lower == "huge" || lower == "massive") return "size:>100mb";
    if (lower == "small")                       return "size:<1mb";
    if (lower == "tiny")                        return "size:<10kb";
    if (lower == "empty" || lower == "zero")    return "size:0";
    if (lower == "gigantic" || lower == "enormous") return "size:>1gb";

    // Check for explicit size like "10mb", "500kb", "2gb"
    if (lower.size() >= 3) {
        size_t unit_pos = std::string::npos;
        std::string unit;
        for (size_t i = lower.size(); i > 0; --i) {
            if (!std::isdigit(static_cast<unsigned char>(lower[i - 1]))) {
                continue;
            }
            unit_pos = i;
            break;
        }
        if (unit_pos != std::string::npos && unit_pos < lower.size()) {
            unit = lower.substr(unit_pos);
            std::string num = lower.substr(0, unit_pos);
            if (!num.empty() && std::all_of(num.begin(), num.end(),
                    [](unsigned char c) { return std::isdigit(c); })) {
                if (unit == "kb" || unit == "k") return "size:>" + num + "kb";
                if (unit == "mb" || unit == "m") return "size:>" + num + "mb";
                if (unit == "gb" || unit == "g") return "size:>" + num + "gb";
                if (unit == "b")                  return "size:>" + num;
            }
        }
    }

    return {};
}

// ============================================================
// Date matching
// ============================================================

std::string match_date_filter(const std::string& word) {
    std::string lower = to_lower(word);

    if (lower == "today")          return "dm:today";
    if (lower == "yesterday")      return "dm:yesterday";
    if (lower == "recent" ||
        lower == "recently")       return "dm:last1hours";

    // Multi-word handled by caller via phrase check
    return {};
}

// Check for two-word date phrases
static std::string match_date_phrase(const std::vector<std::string>& words, size_t i) {
    if (i + 1 < words.size()) {
        std::string two = to_lower(words[i] + " " + words[i + 1]);
        if (two == "last week" || two == "past week")   return "dm:last7days";
        if (two == "last month" || two == "past month")  return "dm:last30days";
        if (two == "last year" || two == "past year")    return "dm:last365days";
        if (two == "this week")  return "dm:thisweek";
        if (two == "this month") return "dm:thismonth";
        if (two == "this year")  return "dm:thisyear";
    }
    return {};
}

// ============================================================
// Path extraction
// ============================================================

std::string extract_path(const std::string& word) {
    // Contains backslash or starts with drive letter
    if (word.find('\\') != std::string::npos) return word;
    if (word.size() >= 2 && word[1] == ':') return word;
    // Starts with / or ~ (Unix-like, rare on Windows but possible)
    if (!word.empty() && (word[0] == '/' || word[0] == '~')) return word;
    return {};
}

// ============================================================
// Main builder
// ============================================================

BuildResult build(const std::string& intent) {
    BuildResult result;
    auto words = split_words(intent);

    std::vector<std::string> query_parts;
    std::vector<std::string> name_parts;

    bool in_exclusion = false;

    for (size_t i = 0; i < words.size(); ++i) {
        std::string lower = to_lower(words[i]);

        // Check for exclusion markers
        if (lower == "not" || lower == "excluding" || lower == "without" ||
            lower == "except" || lower == "skip") {
            in_exclusion = true;
            result.has_exclusion = true;
            result.tokens.push_back("exclusion:" + lower);
            continue;
        }

        // Reset exclusion flag after consuming next token group
        bool apply_exclusion = in_exclusion;
        in_exclusion = false;

        // Try date phrase (two words)
        auto date_phrase = match_date_phrase(words, i);
        if (!date_phrase.empty()) {
            if (apply_exclusion) {
                query_parts.push_back("!" + date_phrase);
            } else {
                query_parts.push_back(date_phrase);
            }
            result.has_date_filter = true;
            result.tokens.push_back("date:" + date_phrase);
            ++i; // consume next word
            continue;
        }

        // Try date single word
        auto date = match_date_filter(words[i]);
        if (!date.empty()) {
            if (apply_exclusion) {
                query_parts.push_back("!" + date);
            } else {
                query_parts.push_back(date);
            }
            result.has_date_filter = true;
            result.tokens.push_back("date:" + date);
            continue;
        }

        // Try size filter
        auto size = match_size_filter(words[i]);
        if (!size.empty()) {
            if (apply_exclusion) {
                query_parts.push_back("!" + size);
            } else {
                query_parts.push_back(size);
            }
            result.has_size_filter = true;
            result.tokens.push_back("size:" + size);
            continue;
        }

        // Try file type
        auto ftype = match_file_type(words[i]);
        if (!ftype.empty()) {
            if (apply_exclusion) {
                query_parts.push_back("!" + ftype);
            } else {
                query_parts.push_back(ftype);
            }
            result.has_file_type = true;
            result.tokens.push_back("type:" + ftype);
            continue;
        }

        // Try path extraction
        auto path = extract_path(words[i]);
        if (!path.empty()) {
            if (apply_exclusion) {
                query_parts.push_back("!path:\"" + path + "\"");
            } else {
                query_parts.push_back("path:\"" + path + "\"");
            }
            result.has_path_filter = true;
            result.tokens.push_back("path:" + path);
            continue;
        }

        // "in" or "under" followed by a path
        if (lower == "in" || lower == "under" || lower == "inside" ||
            lower == "from") {
            if (i + 1 < words.size()) {
                auto next_path = extract_path(words[i + 1]);
                if (!next_path.empty()) {
                    query_parts.push_back("path:\"" + next_path + "\"");
                    result.has_path_filter = true;
                    result.tokens.push_back("path:" + next_path);
                    ++i;
                    continue;
                }
            }
        }

        // Skip common filler words
        if (lower == "find" || lower == "show" || lower == "get" ||
            lower == "list" || lower == "search" || lower == "all" ||
            lower == "the" || lower == "a" || lower == "an" ||
            lower == "of" || lower == "with" || lower == "that" ||
            lower == "are" || lower == "is" || lower == "were" ||
            lower == "and" || lower == "or" || lower == "folder" ||
            lower == "file" || lower == "files" || lower == "folders" ||
            lower == "for" || lower == "me" || lower == "please") {
            continue;
        }

        // Treat as a name/search term
        if (apply_exclusion) {
            query_parts.push_back("!" + words[i]);
        } else {
            name_parts.push_back(words[i]);
        }
        result.tokens.push_back("name:" + words[i]);
    }

    // Assemble query: name parts first, then structured filters
    std::vector<std::string> all_parts;

    // Combine name parts with space (Everything treats space as AND)
    if (!name_parts.empty()) {
        std::string name_query;
        for (size_t i = 0; i < name_parts.size(); ++i) {
            if (i > 0) name_query += " ";
            name_query += name_parts[i];
        }
        all_parts.push_back(name_query);
    }

    // Add structured query parts
    for (const auto& p : query_parts) {
        all_parts.push_back(p);
    }

    // Join with space
    result.query.clear();
    for (size_t i = 0; i < all_parts.size(); ++i) {
        if (i > 0) result.query += " ";
        result.query += all_parts[i];
    }

    result.query = trim(result.query);
    return result;
}

} // namespace smart_query
