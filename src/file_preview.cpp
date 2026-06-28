//
// file_preview.cpp — Unified file preview with type-aware metadata extraction.
//

#include "file_preview.hpp"
#include "content_search.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <string>
#include <windows.h>

namespace file_preview {

// ============================================================
// Helpers
// ============================================================

static std::string to_lower_ext(const std::string& ext) {
    std::string lower = ext;
    if (!lower.empty() && lower[0] == '.') lower.erase(0, 1);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return lower;
}

static uint16_t read_u16_be(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           p[3];
}

static uint32_t read_u32_le(const uint8_t* p) {
    return p[0] |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// ============================================================
// Categorization
// ============================================================

std::string categorize(const std::string& extension) {
    std::string ext = to_lower_ext(extension);

    // PE binaries
    if (ext == "exe" || ext == "dll" || ext == "sys" || ext == "msi" ||
        ext == "drv" || ext == "ocx")
        return "pe_binary";

    // Images
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
        ext == "bmp" || ext == "webp" || ext == "ico" || ext == "tiff" ||
        ext == "tif")
        return "image";

    // Archives
    if (ext == "zip")
        return "archive";

    // Text (reuse content_search extension detection)
    if (content_search::is_text_extension(ext))
        return "text";

    return "generic";
}

// ============================================================
// Basic file info
// ============================================================

FileBasicInfo get_basic_info(const std::string& path) {
    FileBasicInfo info;
    info.path = path;

    std::wstring wide = str_utils::utf8_to_wide(path);

    // Extract filename from path
    size_t slash = path.find_last_of("\\/");
    info.file_name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    // Extract extension
    size_t dot = info.file_name.find_last_of('.');
    info.extension = (dot != std::string::npos)
        ? info.file_name.substr(dot + 1) : "";

    // File attributes and size
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &fad)) {
        info.is_folder = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.is_readonly = (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        info.is_hidden = (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;

        LARGE_INTEGER li;
        li.LowPart = fad.nFileSizeLow;
        li.HighPart = fad.nFileSizeHigh;
        info.size = li.QuadPart;

        // Format dates
        auto format_filetime = [](const FILETIME& ft) -> std::string {
            SYSTEMTIME st;
            if (FileTimeToSystemTime(&ft, &st)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond);
                return buf;
            }
            return {};
        };
        info.date_modified = format_filetime(fad.ftLastWriteTime);
        info.date_created = format_filetime(fad.ftCreationTime);
    }

    return info;
}

// ============================================================
// PE metadata extraction
// ============================================================

std::optional<PeMetadata> extract_pe_metadata(const std::string& path) {
    std::wstring wide = str_utils::utf8_to_wide(path);

    // Determine version info size
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(wide.c_str(), &handle);
    if (size == 0) return std::nullopt;

    // Allocate and read version info
    std::vector<uint8_t> buffer(size);
    if (!GetFileVersionInfoW(wide.c_str(), handle, size, buffer.data()))
        return std::nullopt;

    PeMetadata meta;

    // Helper to query a string value
    auto query_string = [&](const wchar_t* name) -> std::string {
        wchar_t* value = nullptr;
        UINT len = 0;
        // Use translation table to find the right block
        struct LANGCODEPAGE {
            WORD wLanguage;
            WORD wCodePage;
        } *lpTranslate = nullptr;
        UINT cbTranslate = 0;

        // First try English\Unicode code page
        std::wstring query = L"\\StringFileInfo\\040904B0\\" + std::wstring(name);
        if (VerQueryValueW(buffer.data(), query.c_str(),
                           reinterpret_cast<LPVOID*>(&value), &len) && value) {
            return str_utils::wide_to_utf8(value);
        }

        // Fall back: enumerate translations and use the first one
        if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                           reinterpret_cast<LPVOID*>(&lpTranslate), &cbTranslate) &&
            cbTranslate >= sizeof(LANGCODEPAGE)) {
            wchar_t sub_block[256];
            std::swprintf(sub_block, 256, L"\\StringFileInfo\\%04x%04x\\%s",
                lpTranslate[0].wLanguage, lpTranslate[0].wCodePage, name);
            if (VerQueryValueW(buffer.data(), sub_block,
                               reinterpret_cast<LPVOID*>(&value), &len) && value) {
                return str_utils::wide_to_utf8(value);
            }
        }

        return {};
    };

    meta.file_version      = query_string(L"FileVersion");
    meta.product_version   = query_string(L"ProductVersion");
    meta.company_name      = query_string(L"CompanyName");
    meta.file_description  = query_string(L"FileDescription");
    meta.product_name      = query_string(L"ProductName");
    meta.original_filename = query_string(L"OriginalFilename");
    meta.internal_name     = query_string(L"InternalName");
    meta.legal_copyright   = query_string(L"LegalCopyright");

    return meta;
}

// ============================================================
// Image dimension parsing
// ============================================================

static std::optional<ImageMetadata> parse_png(const uint8_t* data, size_t size) {
    // PNG: 8-byte signature, then IHDR chunk
    // Signature: 89 50 4E 47 0D 0A 1A 0A
    if (size < 24) return std::nullopt;
    static const uint8_t png_sig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data, png_sig, 8) != 0) return std::nullopt;

    // IHDR: 4 bytes length, 4 bytes "IHDR", then width(4 BE), height(4 BE)
    if (read_u32_be(data + 12) != 0x49484452) return std::nullopt; // "IHDR"

    ImageMetadata img;
    img.format = "PNG";
    img.width = static_cast<int>(read_u32_be(data + 16));
    img.height = static_cast<int>(read_u32_be(data + 20));
    return img;
}

static std::optional<ImageMetadata> parse_jpeg(const uint8_t* data, size_t size) {
    // JPEG: starts with FF D8, scan for SOF markers
    if (size < 4) return std::nullopt;
    if (data[0] != 0xFF || data[1] != 0xD8) return std::nullopt;

    size_t pos = 2;
    while (pos + 4 < size) {
        if (data[pos] != 0xFF) { ++pos; continue; }

        uint8_t marker = data[pos + 1];

        // SOF0 (0xC0), SOF1 (0xC1), SOF2 (0xC2), SOF3 (0xC3),
        // SOF5-F7, SOF9-SOF11, SOF13-SOF15
        if (marker >= 0xC0 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (pos + 9 >= size) break;
            ImageMetadata img;
            img.format = "JPEG";
            // After marker (2 bytes) + length (2 bytes) + precision (1 byte):
            // height (2 BE), width (2 BE)
            img.height = read_u16_be(data + pos + 5);
            img.width = read_u16_be(data + pos + 7);
            return img;
        }

        // Skip this marker segment
        if (pos + 4 >= size) break;
        uint16_t seg_len = read_u16_be(data + pos + 2);
        pos += 2 + seg_len;
    }

    return std::nullopt;
}

static std::optional<ImageMetadata> parse_gif(const uint8_t* data, size_t size) {
    // GIF: "GIF87a" or "GIF89a", then logical screen descriptor
    if (size < 13) return std::nullopt;
    if (std::memcmp(data, "GIF", 3) != 0) return std::nullopt;
    if (data[3] != '8') return std::nullopt;

    ImageMetadata img;
    img.format = (data[4] == '7') ? "GIF87a" : "GIF89a";
    img.width = read_u16_le(data + 6);
    img.height = read_u16_le(data + 8);
    return img;
}

static std::optional<ImageMetadata> parse_bmp(const uint8_t* data, size_t size) {
    // BMP: "BM" header, DIB header at offset 14
    if (size < 26) return std::nullopt;
    if (data[0] != 'B' || data[1] != 'M') return std::nullopt;

    // BITMAPINFOHEADER (40 bytes) starts at offset 14
    // Width at offset 18 (int32 LE), height at offset 22 (int32 LE)
    uint32_t header_size = read_u32_le(data + 14);

    ImageMetadata img;
    img.format = "BMP";
    img.width = static_cast<int32_t>(read_u32_le(data + 18));
    // Height can be negative (top-down bitmap)
    int32_t h = static_cast<int32_t>(read_u32_le(data + 22));
    img.height = (h < 0) ? -h : h;
    if (header_size >= 40) {
        img.details = "bpp:" + std::to_string(read_u16_le(data + 28));
    }
    return img;
}

static std::optional<ImageMetadata> parse_webp(const uint8_t* data, size_t size) {
    // WebP: RIFF header (4 bytes "RIFF"), file size (4 LE), "WEBP" (4 bytes)
    // Then VP8, VP8L, or VP8X chunk
    if (size < 30) return std::nullopt;
    if (std::memcmp(data, "RIFF", 4) != 0) return std::nullopt;
    if (std::memcmp(data + 8, "WEBP", 4) != 0) return std::nullopt;

    // Chunk type at offset 12
    if (std::memcmp(data + 12, "VP8X", 4) == 0) {
        // VP8X extended format
        // Canvas width: 24 bits at offset 24 (3 bytes, LE, 1-based)
        // Canvas height: 24 bits at offset 27 (3 bytes, LE, 1-based)
        if (size < 30) return std::nullopt;
        uint32_t w = (static_cast<uint32_t>(data[24])) |
                     (static_cast<uint32_t>(data[25]) << 8) |
                     (static_cast<uint32_t>(data[26]) << 16);
        uint32_t h = (static_cast<uint32_t>(data[27])) |
                     (static_cast<uint32_t>(data[28]) << 8) |
                     (static_cast<uint32_t>(data[29]) << 16);

        ImageMetadata img;
        img.format = "WebP";
        img.width = static_cast<int>(w) + 1;
        img.height = static_cast<int>(h) + 1;
        img.details = "extended";
        return img;
    }

    if (std::memcmp(data + 12, "VP8 ", 4) == 0) {
        // VP8 lossy: chunk header (4 type + 4 size), then 3-byte frame tag,
        // then width (16 LE) and height (16 LE)
        // VP8 chunk data starts at offset 20
        if (size < 30) return std::nullopt;
        // Skip frame tag (3 bytes at offset 20)
        uint16_t w = read_u16_le(data + 26) & 0x3FFF;
        uint16_t h = read_u16_le(data + 28) & 0x3FFF;

        ImageMetadata img;
        img.format = "WebP";
        img.width = w;
        img.height = h;
        img.details = "VP8";
        return img;
    }

    if (std::memcmp(data + 12, "VP8L", 4) == 0) {
        // VP8L lossless: chunk header (4 type + 4 size), then 1-byte signature (0x2f),
        // then width-1 (14 bits), height-1 (14 bits)
        // VP8L data starts at offset 20
        if (size < 25) return std::nullopt;
        if (data[20] != 0x2f) return std::nullopt;

        // Bits are packed in LSB order
        uint32_t b0 = data[21];
        uint32_t b1 = data[22];
        uint32_t b2 = data[23];
        uint32_t b3 = data[24];

        uint32_t w_bits = b0 | ((b1 & 0x3F) << 8);
        uint32_t h_bits = (b1 >> 6) | (b2 << 2) | ((b3 & 0x0F) << 10);

        ImageMetadata img;
        img.format = "WebP";
        img.width = static_cast<int>(w_bits) + 1;
        img.height = static_cast<int>(h_bits) + 1;
        img.details = "VP8L";
        return img;
    }

    return std::nullopt; // unrecognized WebP variant
}

std::optional<ImageMetadata> extract_image_info(const uint8_t* data, size_t size,
                                                const std::string& extension) {
    std::string ext = to_lower_ext(extension);

    if (ext == "png")                    return parse_png(data, size);
    if (ext == "jpg" || ext == "jpeg")   return parse_jpeg(data, size);
    if (ext == "gif")                    return parse_gif(data, size);
    if (ext == "bmp")                    return parse_bmp(data, size);
    if (ext == "webp")                   return parse_webp(data, size);

    return std::nullopt;
}

// ============================================================
// ZIP listing
// ============================================================

std::optional<ZipListing> extract_zip_listing(const uint8_t* data, size_t size,
                                               int max_entries) {
    // ZIP central directory entries start with PK\x01\x02
    // Scan backwards for End of Central Directory (EOCD): PK\x05\x06
    if (size < 22) return std::nullopt;

    // Find EOCD by scanning backwards
    const uint8_t eocd_sig[] = {0x50, 0x4B, 0x05, 0x06};
    size_t eocd_pos = std::string::npos;

    // Search in the last 65557 bytes (max EOCD comment is 65535)
    size_t search_start = (size > 65557) ? size - 65557 : 0;
    for (size_t i = size - 4; i >= search_start; --i) {
        if (std::memcmp(data + i, eocd_sig, 4) == 0) {
            eocd_pos = i;
            break;
        }
        if (i == 0) break;
    }

    if (eocd_pos == std::string::npos) return std::nullopt;

    // EOCD: sig(4) + disk(2) + cd_disk(2) + cd_entries_disk(2) + cd_entries_total(2)
    //        + cd_size(4) + cd_offset(4) + comment_len(2)
    if (eocd_pos + 22 > size) return std::nullopt;

    uint16_t total_entries = read_u16_le(data + eocd_pos + 10);
    uint32_t cd_offset = read_u32_le(data + eocd_pos + 16);

    ZipListing listing;
    listing.entry_count = total_entries;

    // Parse central directory entries
    const uint8_t cd_sig[] = {0x50, 0x4B, 0x01, 0x02};
    size_t pos = cd_offset;
    int parsed = 0;

    while (pos + 46 <= size && parsed < total_entries && parsed < max_entries) {
        if (std::memcmp(data + pos, cd_sig, 4) != 0) break;

        // Central directory file header layout:
        // 0: sig(4), 4: version_made(2), 6: version_needed(2),
        // 8: flags(2), 10: compression(2), 12: mod_time(2), 14: mod_date(2),
        // 16: crc32(4), 20: compressed_size(4), 24: uncompressed_size(4),
        // 28: filename_len(2), 30: extra_len(2), 32: comment_len(2),
        // 34: disk_start(2), 36: internal_attr(2), 38: external_attr(4),
        // 42: local_header_offset(4), 46: filename...

        uint32_t compressed = read_u32_le(data + pos + 20);
        uint32_t uncompressed = read_u32_le(data + pos + 24);
        uint16_t name_len = read_u16_le(data + pos + 28);
        uint16_t extra_len = read_u16_le(data + pos + 30);
        uint16_t comment_len = read_u16_le(data + pos + 32);

        if (pos + 46 + name_len > size) break;

        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(data + pos + 46), name_len);
        entry.compressed_size = compressed;
        entry.uncompressed_size = uncompressed;
        listing.total_uncompressed += uncompressed;
        listing.entries.push_back(std::move(entry));

        ++parsed;
        pos += 46 + name_len + extra_len + comment_len;
    }

    if (parsed < total_entries) {
        listing.truncated = true;
        listing.entry_count = parsed;
    }

    return listing;
}

// ============================================================
// Text preview
// ============================================================

TextPreview extract_text_preview(const std::string& content, int max_lines) {
    TextPreview preview;
    preview.total_lines = 0;

    // Split into lines (handle CRLF and LF)
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            ++preview.total_lines;
            if (static_cast<int>(preview.lines.size()) < max_lines) {
                size_t end = i;
                if (end > start && content[end - 1] == '\r') --end;
                preview.lines.emplace_back(content.substr(start, end - start));
            }
            start = i + 1;
        }
    }
    // Handle last line without trailing newline
    if (start < content.size()) {
        ++preview.total_lines;
        if (static_cast<int>(preview.lines.size()) < max_lines) {
            size_t end = content.size();
            if (end > start && content[end - 1] == '\r') --end;
            preview.lines.emplace_back(content.substr(start, end - start));
        }
    }

    preview.shown_lines = static_cast<int>(preview.lines.size());
    preview.truncated = preview.total_lines > preview.shown_lines;
    return preview;
}

// ============================================================
// Main preview dispatcher
// ============================================================

PreviewResult preview(const std::string& path, int max_lines) {
    PreviewResult result;
    result.basic = get_basic_info(path);

    if (result.basic.path.empty()) {
        result.error_message = "Invalid path";
        return result;
    }

    // Verify file exists
    std::wstring wide = str_utils::utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        result.error_message = "File does not exist: " + path;
        return result;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        result.category = "directory";
        return result;
    }

    std::string cat = categorize(result.basic.extension);
    result.category = cat;

    if (cat == "text") {
        // Read text content (10 MB max)
        auto content = content_search::read_text_file(path);
        if (content) {
            result.text = extract_text_preview(*content, max_lines);
        } else {
            result.category = "generic"; // fallback
        }
    }

    if (cat == "pe_binary") {
        auto pe = extract_pe_metadata(path);
        if (pe) {
            result.pe = std::move(*pe);
        }
    }

    if (cat == "image") {
        // Read first 256 bytes for header parsing
        std::ifstream file(path, std::ios::binary);
        if (file) {
            std::vector<uint8_t> header(256);
            file.read(reinterpret_cast<char*>(header.data()), 256);
            size_t bytes_read = static_cast<size_t>(file.gcount());
            auto img = extract_image_info(header.data(), bytes_read,
                                          result.basic.extension);
            if (img) {
                result.image = std::move(*img);
            }
        }
    }

    if (cat == "archive") {
        // Read entire file for ZIP parsing (cap at 100 MB)
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file) {
            auto file_size = file.tellg();
            if (file_size > 0 && file_size <= 100 * 1024 * 1024) {
                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(static_cast<size_t>(file_size));
                file.read(reinterpret_cast<char*>(raw.data()), file_size);
                auto zip = extract_zip_listing(raw.data(), raw.size());
                if (zip) {
                    result.archive = std::move(*zip);
                }
            } else if (file_size > 100 * 1024 * 1024) {
                // Too large to read into memory
                result.error_message = "Archive exceeds 100MB preview limit";
            }
        }
    }

    return result;
}

} // namespace file_preview
