#pragma once

//
// file_preview.hpp — Unified file preview with type-aware metadata extraction.
// Dispatches by extension: text, PE binary, image, archive, or generic.
//

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace file_preview {

struct TextPreview {
    int total_lines = 0;
    int shown_lines = 0;
    std::vector<std::string> lines;
    bool truncated = false;
};

struct PeMetadata {
    std::string file_version;
    std::string product_version;
    std::string company_name;
    std::string file_description;
    std::string product_name;
    std::string original_filename;
    std::string internal_name;
    std::string legal_copyright;
};

struct ImageMetadata {
    int width = 0;
    int height = 0;
    std::string format;     // "PNG", "JPEG", "GIF", "BMP", "WebP"
    std::string details;    // extra format-specific info
};

struct ZipEntry {
    std::string name;
    int64_t uncompressed_size = 0;
    int64_t compressed_size = 0;
};

struct ZipListing {
    int entry_count = 0;
    int64_t total_uncompressed = 0;
    std::vector<ZipEntry> entries;
    bool truncated = false;
};

struct FileBasicInfo {
    std::string path;
    std::string file_name;
    std::string extension;
    int64_t size = 0;
    std::string date_modified;
    std::string date_created;
    bool is_folder = false;
    bool is_readonly = false;
    bool is_hidden = false;
};

struct PreviewResult {
    std::string category;   // "text", "pe_binary", "image", "archive", "generic"
    FileBasicInfo basic;
    std::optional<TextPreview> text;
    std::optional<PeMetadata> pe;
    std::optional<ImageMetadata> image;
    std::optional<ZipListing> archive;
    std::string error_message;
};

// Preview a file by path. auto-detects type from extension.
PreviewResult preview(const std::string& path, int max_lines = 100);

// --- Individual extractors (exposed for testing) ---

std::optional<PeMetadata> extract_pe_metadata(const std::string& path);

// Parse image dimensions from raw header bytes.
// data must contain at least the first 64 bytes of the file.
std::optional<ImageMetadata> extract_image_info(const uint8_t* data, size_t size,
                                                const std::string& extension);

// Parse ZIP central directory from raw file data.
std::optional<ZipListing> extract_zip_listing(const uint8_t* data, size_t size,
                                              int max_entries = 500);

// Extract first N lines from text content.
TextPreview extract_text_preview(const std::string& content, int max_lines);

// Categorize a file by extension.
std::string categorize(const std::string& extension);

// Get basic file info.
FileBasicInfo get_basic_info(const std::string& path);

} // namespace file_preview
