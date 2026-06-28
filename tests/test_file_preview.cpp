//
// test_file_preview.cpp — Tests for file preview and metadata extraction.
//

#include "test_framework.hpp"
#include "../src/file_preview.hpp"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

using namespace file_preview;

// ============================================================
// Categorization tests
// ============================================================

TEST(preview_categorize_text) {
    CHECK_STR_EQ(categorize("cpp"), "text");
    CHECK_STR_EQ(categorize("py"), "text");
    CHECK_STR_EQ(categorize("json"), "text");
    CHECK_STR_EQ(categorize("md"), "text");
}

TEST(preview_categorize_pe) {
    CHECK_STR_EQ(categorize("exe"), "pe_binary");
    CHECK_STR_EQ(categorize("dll"), "pe_binary");
    CHECK_STR_EQ(categorize("sys"), "pe_binary");
}

TEST(preview_categorize_image) {
    CHECK_STR_EQ(categorize("png"), "image");
    CHECK_STR_EQ(categorize("jpg"), "image");
    CHECK_STR_EQ(categorize("gif"), "image");
    CHECK_STR_EQ(categorize("bmp"), "image");
    CHECK_STR_EQ(categorize("webp"), "image");
}

TEST(preview_categorize_archive) {
    CHECK_STR_EQ(categorize("zip"), "archive");
}

TEST(preview_categorize_generic) {
    CHECK_STR_EQ(categorize("xyz"), "generic");
    CHECK_STR_EQ(categorize("dat"), "generic");
}

TEST(preview_categorize_strips_dot) {
    CHECK_STR_EQ(categorize(".cpp"), "text");
    CHECK_STR_EQ(categorize(".exe"), "pe_binary");
}

// ============================================================
// Text preview extraction
// ============================================================

TEST(preview_text_basic) {
    std::string content = "line 1\nline 2\nline 3\nline 4\nline 5\n";
    auto tp = extract_text_preview(content, 100);
    CHECK_EQ(tp.total_lines, 5);
    CHECK_EQ(tp.shown_lines, 5);
    CHECK(!tp.truncated);
    CHECK_STR_EQ(tp.lines[0], "line 1");
    CHECK_STR_EQ(tp.lines[4], "line 5");
}

TEST(preview_text_truncated) {
    std::string content;
    for (int i = 0; i < 200; ++i) {
        content += "line " + std::to_string(i) + "\n";
    }
    auto tp = extract_text_preview(content, 10);
    CHECK_EQ(tp.total_lines, 200);
    CHECK_EQ(tp.shown_lines, 10);
    CHECK(tp.truncated);
}

TEST(preview_text_crlf) {
    std::string content = "line 1\r\nline 2\r\nline 3\r\n";
    auto tp = extract_text_preview(content, 100);
    CHECK_EQ(tp.total_lines, 3);
    CHECK_STR_EQ(tp.lines[0], "line 1");
    CHECK_STR_EQ(tp.lines[1], "line 2");
}

TEST(preview_text_no_trailing_newline) {
    std::string content = "line 1\nline 2\nline 3";
    auto tp = extract_text_preview(content, 100);
    CHECK_EQ(tp.total_lines, 3);
    CHECK_STR_EQ(tp.lines[2], "line 3");
}

TEST(preview_text_empty) {
    std::string content;
    auto tp = extract_text_preview(content, 100);
    CHECK_EQ(tp.total_lines, 0);
    CHECK_EQ(tp.shown_lines, 0);
}

// ============================================================
// Image dimension parsing
// ============================================================

TEST(preview_png_dimensions) {
    // Minimal PNG with known dimensions (4x4 pixel)
    // PNG signature + IHDR chunk
    static const uint8_t png_4x4[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // signature
        0x00, 0x00, 0x00, 0x0D,  // IHDR chunk length (13)
        0x49, 0x48, 0x44, 0x52,  // "IHDR"
        0x00, 0x00, 0x00, 0x04,  // width = 4
        0x00, 0x00, 0x00, 0x04,  // height = 4
        0x08, 0x02, 0x00, 0x00, 0x00,  // bit depth, color type, etc.
    };

    auto img = extract_image_info(png_4x4, sizeof(png_4x4), "png");
    CHECK(img.has_value());
    CHECK_STR_EQ(img->format, "PNG");
    CHECK_EQ(img->width, 4);
    CHECK_EQ(img->height, 4);
}

TEST(preview_gif_dimensions) {
    // Minimal GIF header with known dimensions (10x10)
    static const uint8_t gif_10x10[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61,  // "GIF89a"
        0x0A, 0x00,  // width = 10 (LE)
        0x0A, 0x00,  // height = 10 (LE)
        0x00, 0x00, 0x00,  // packed, bg color, aspect ratio
    };

    auto img = extract_image_info(gif_10x10, sizeof(gif_10x10), "gif");
    CHECK(img.has_value());
    CHECK_EQ(img->width, 10);
    CHECK_EQ(img->height, 10);
}

TEST(preview_bmp_dimensions) {
    // Minimal BMP header (DIB header starts at offset 14)
    static const uint8_t bmp_8x6[] = {
        0x42, 0x4D,             // "BM"
        0x00, 0x00, 0x00, 0x00, // file size (don't care)
        0x00, 0x00, 0x00, 0x00, // reserved
        0x36, 0x00, 0x00, 0x00, // data offset
        0x28, 0x00, 0x00, 0x00, // DIB header size (40)
        0x08, 0x00, 0x00, 0x00, // width = 8 (LE)
        0x06, 0x00, 0x00, 0x00, // height = 6 (LE)
        0x01, 0x00,             // planes
        0x18, 0x00,             // bpp = 24
    };

    auto img = extract_image_info(bmp_8x6, sizeof(bmp_8x6), "bmp");
    CHECK(img.has_value());
    CHECK_STR_EQ(img->format, "BMP");
    CHECK_EQ(img->width, 8);
    CHECK_EQ(img->height, 6);
}

TEST(preview_image_unknown_format) {
    static const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    auto img = extract_image_info(garbage, sizeof(garbage), "png");
    CHECK(!img.has_value());
}

// ============================================================
// ZIP listing
// ============================================================

// Helper: create a minimal ZIP file in memory
static std::vector<uint8_t> make_minimal_zip(
    const std::vector<std::pair<std::string, uint32_t>>& entries) {

    std::vector<uint8_t> zip;

    // Local file headers + file data
    std::vector<uint32_t> local_offsets;
    for (const auto& [name, size] : entries) {
        local_offsets.push_back(static_cast<uint32_t>(zip.size()));

        // Local file header signature: PK\x03\x04
        uint8_t local_header[] = {
            0x50, 0x4B, 0x03, 0x04,
            20, 0,                  // version needed
            0, 0,                   // flags
            0, 0,                   // compression (stored)
            0, 0, 0, 0,             // mod time/date
            0, 0, 0, 0,             // CRC32
            static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>((size >> 8) & 0xFF),
            0, 0,                   // compressed size
            static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>((size >> 8) & 0xFF),
            0, 0,                   // uncompressed size
            static_cast<uint8_t>(name.size() & 0xFF), static_cast<uint8_t>((name.size() >> 8) & 0xFF),
            0, 0,                   // extra length
        };
        zip.insert(zip.end(), local_header, local_header + sizeof(local_header));

        // Filename
        zip.insert(zip.end(), name.begin(), name.end());

        // File data (zeros)
        zip.insert(zip.end(), size, 0);
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());

    // Central directory entries
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& [name, size] = entries[i];

        uint8_t cd_header[] = {
            0x50, 0x4B, 0x01, 0x02,
            20, 0,                  // version made
            20, 0,                  // version needed
            0, 0,                   // flags
            0, 0,                   // compression (stored)
            0, 0, 0, 0,             // mod time/date
            0, 0, 0, 0,             // CRC32
            static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>((size >> 8) & 0xFF),
            0, 0,                   // compressed size
            static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>((size >> 8) & 0xFF),
            0, 0,                   // uncompressed size
            static_cast<uint8_t>(name.size() & 0xFF), static_cast<uint8_t>((name.size() >> 8) & 0xFF),
            0, 0,                   // extra length
            0, 0,                   // comment length
            0, 0,                   // disk number
            0, 0,                   // internal attrs
            0, 0, 0, 0,             // external attrs
            static_cast<uint8_t>(local_offsets[i] & 0xFF),
            static_cast<uint8_t>((local_offsets[i] >> 8) & 0xFF),
            static_cast<uint8_t>((local_offsets[i] >> 16) & 0xFF),
            static_cast<uint8_t>((local_offsets[i] >> 24) & 0xFF),
        };
        zip.insert(zip.end(), cd_header, cd_header + sizeof(cd_header));
        zip.insert(zip.end(), name.begin(), name.end());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;

    // End of central directory
    uint8_t eocd[] = {
        0x50, 0x4B, 0x05, 0x06,
        0, 0,                   // disk number
        0, 0,                   // CD disk
        static_cast<uint8_t>(entries.size() & 0xFF),
        static_cast<uint8_t>((entries.size() >> 8) & 0xFF),
        static_cast<uint8_t>(entries.size() & 0xFF),
        static_cast<uint8_t>((entries.size() >> 8) & 0xFF),
        static_cast<uint8_t>(cd_size & 0xFF),
        static_cast<uint8_t>((cd_size >> 8) & 0xFF),
        static_cast<uint8_t>((cd_size >> 16) & 0xFF),
        static_cast<uint8_t>((cd_size >> 24) & 0xFF),
        static_cast<uint8_t>(cd_offset & 0xFF),
        static_cast<uint8_t>((cd_offset >> 8) & 0xFF),
        static_cast<uint8_t>((cd_offset >> 16) & 0xFF),
        static_cast<uint8_t>((cd_offset >> 24) & 0xFF),
        0, 0,                   // comment length
    };
    zip.insert(zip.end(), eocd, eocd + sizeof(eocd));

    return zip;
}

TEST(preview_zip_three_entries) {
    auto zip_data = make_minimal_zip({
        {"file1.txt", 100},
        {"dir/file2.txt", 200},
        {"file3.dat", 300},
    });

    auto listing = extract_zip_listing(zip_data.data(), zip_data.size());
    CHECK(listing.has_value());
    CHECK_EQ(listing->entry_count, 3);
    CHECK(!listing->truncated);
    CHECK_EQ(listing->entries.size(), 3u);
    CHECK_STR_EQ(listing->entries[0].name, "file1.txt");
    CHECK_EQ(listing->entries[0].uncompressed_size, 100u);
    CHECK_STR_EQ(listing->entries[1].name, "dir/file2.txt");
    CHECK_EQ(listing->entries[1].uncompressed_size, 200u);
    CHECK_STR_EQ(listing->entries[2].name, "file3.dat");
    CHECK_EQ(listing->entries[2].uncompressed_size, 300u);
    CHECK_EQ(listing->total_uncompressed, 600u);
}

TEST(preview_zip_empty) {
    auto zip_data = make_minimal_zip({});
    auto listing = extract_zip_listing(zip_data.data(), zip_data.size());
    CHECK(listing.has_value());
    CHECK_EQ(listing->entry_count, 0);
    CHECK_EQ(listing->entries.size(), 0u);
}

TEST(preview_zip_garbage) {
    static const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    auto listing = extract_zip_listing(garbage, sizeof(garbage));
    CHECK(!listing.has_value());
}

TEST(preview_zip_truncation) {
    auto zip_data = make_minimal_zip({
        {"a.txt", 10},
        {"b.txt", 20},
        {"c.txt", 30},
    });

    auto listing = extract_zip_listing(zip_data.data(), zip_data.size(), 2);
    CHECK(listing.has_value());
    CHECK(listing->truncated);
    CHECK_EQ(static_cast<int>(listing->entries.size()), 2);
}

// ============================================================
// PE metadata (tests against the test binary itself)
// ============================================================

TEST(preview_pe_metadata_from_test_binary) {
    // The test binary should have version info or at least not crash
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    auto meta = extract_pe_metadata(path);
    // The test binary might not have version info, so just verify it doesn't crash
    // and returns either valid data or nullopt
    // If it returns data, verify at least some field
    if (meta) {
        // Some field should be non-empty for a valid PE
        bool any_field = !meta->file_version.empty() ||
                         !meta->product_version.empty() ||
                         !meta->company_name.empty();
        CHECK(any_field);
    }
}

// ============================================================
// Full preview dispatcher
// ============================================================

TEST(preview_text_file) {
    // Create temp text file
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "prv", 0, temp_path);
    {
        std::ofstream f(temp_path);
        f << "line 1\nline 2\nline 3\n";
    }

    // Rename to .txt for correct categorization
    std::string txt_path = std::string(temp_path) + ".txt";
    rename(temp_path, txt_path.c_str());

    auto pr = preview(txt_path, 100);
    CHECK_STR_EQ(pr.category, "text");
    CHECK(pr.text.has_value());
    CHECK_EQ(pr.text->total_lines, 3);
    CHECK_EQ(pr.text->shown_lines, 3);

    DeleteFileA(txt_path.c_str());
}

TEST(preview_nonexistent_file) {
    auto pr = preview("Z:\\nonexistent\\file.xyz");
    CHECK(!pr.error_message.empty());
}

TEST(preview_basic_info) {
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "prv", 0, temp_path);

    auto info = get_basic_info(temp_path);
    CHECK_STR_EQ(info.path, temp_path);
    CHECK(!info.file_name.empty());
    CHECK(!info.is_folder);

    DeleteFileA(temp_path);
}
