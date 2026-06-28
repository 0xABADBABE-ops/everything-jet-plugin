//
// test_file_hashing.cpp — Tests for CNG MD5 hashing.
//

#include "test_framework.hpp"
#include "../src/file_hashing.hpp"

#include <cstring>
#include <fstream>
#include <string>

using namespace file_hashing;

// Known MD5 test vectors (RFC 1321)
TEST(md5_empty_string) {
    std::string empty;
    auto hash = md5_hex_string(empty);
    CHECK(!hash.empty());
    CHECK_STR_EQ(hash, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(md5_abc) {
    std::string data = "abc";
    auto hash = md5_hex_string(data);
    CHECK_STR_EQ(hash, "900150983cd24fb0d6963f7d28e17f72");
}

TEST(md5_message_digest) {
    std::string data = "message digest";
    auto hash = md5_hex_string(data);
    CHECK_STR_EQ(hash, "f96b697d7cb7938d525a2f31aaf161d0");
}

TEST(md5_alphabet) {
    std::string data = "abcdefghijklmnopqrstuvwxyz";
    auto hash = md5_hex_string(data);
    CHECK_STR_EQ(hash, "c3fcd3d76192e4007dfb496cca67e13b");
}

TEST(md5_long_string) {
    // 56 characters — exactly one block after padding
    std::string data = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    auto hash = md5_hex_string(data);
    CHECK_STR_EQ(hash, "d174ab98d277d9f5a5611c2c9f419d9f");
}

TEST(md5_buffer_null_data) {
    // Hashing a null buffer with size 0 should be valid (empty hash)
    auto hash = md5_hex(nullptr, 0);
    CHECK_STR_EQ(hash, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(md5_file_small) {
    // Create a temp file with known content
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "md5", 0, temp_path);
    {
        std::ofstream f(temp_path, std::ios::binary);
        f << "abc";
    }

    auto hash = md5_file(temp_path);
    CHECK(hash.has_value());
    CHECK_STR_EQ(*hash, "900150983cd24fb0d6963f7d28e17f72");

    DeleteFileA(temp_path);
}

TEST(md5_file_nonexistent) {
    auto hash = md5_file("Z:\\nonexistent\\file.xyz");
    CHECK(!hash.has_value());
}

TEST(md5_file_empty) {
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "md5", 0, temp_path);
    // File is created empty by GetTempFileNameA

    auto hash = md5_file(temp_path);
    CHECK(hash.has_value());
    CHECK_STR_EQ(*hash, "d41d8cd98f00b204e9800998ecf8427e");

    DeleteFileA(temp_path);
}

TEST(md5_file_large_streaming) {
    // Create a file larger than the chunk size (1 MB)
    char temp_path[MAX_PATH];
    GetTempFileNameA(".", "md5", 0, temp_path);
    {
        std::ofstream f(temp_path, std::ios::binary);
        // Write 2.5 MB of 'A' characters
        std::string chunk(1024 * 1024, 'A');
        f.write(chunk.data(), chunk.size());
        f.write(chunk.data(), chunk.size());
        f.write(chunk.data(), chunk.size() / 2);
    }

    // Hash the file
    auto file_hash = md5_file(temp_path);
    CHECK(file_hash.has_value());

    // Hash the same content in memory
    std::string content(1024 * 1024 * 2 + 1024 * 512, 'A');
    auto mem_hash = md5_hex_string(content);

    // They should match
    CHECK_STR_EQ(*file_hash, mem_hash);

    DeleteFileA(temp_path);
}

TEST(md5_context_batch_hashing) {
    Md5Context ctx;
    CHECK(ctx.valid());

    // Create two temp files
    char temp1[MAX_PATH], temp2[MAX_PATH];
    GetTempFileNameA(".", "md5", 0, temp1);
    GetTempFileNameA(".", "md5", 0, temp2);
    {
        std::ofstream f1(temp1, std::ios::binary);
        f1 << "test content";
        std::ofstream f2(temp2, std::ios::binary);
        f2 << "test content";
    }

    auto hash1 = ctx.hash_file(temp1);
    auto hash2 = ctx.hash_file(temp2);
    CHECK(hash1.has_value());
    CHECK(hash2.has_value());
    CHECK_STR_EQ(*hash1, *hash2);  // same content = same hash

    DeleteFileA(temp1);
    DeleteFileA(temp2);
}
