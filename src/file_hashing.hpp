#pragma once

//
// file_hashing.hpp — File content hashing using Windows CNG (BCrypt) API.
// Provides MD5 hashing for content-based duplicate detection.
//

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace file_hashing {

// Hash a buffer in memory. Returns lowercase hex string.
std::string md5_hex(const uint8_t* data, size_t size);

// Hash a string in memory. Convenience wrapper.
std::string md5_hex_string(const std::string& data);

// Hash a file incrementally (1 MB chunks via BCryptHashData).
// Returns nullopt if the file cannot be opened.
std::optional<std::string> md5_file(const std::string& path);

// --- CNG context for batch hashing (avoids repeated init/tear) ---

class Md5Context {
public:
    Md5Context();
    ~Md5Context();

    Md5Context(const Md5Context&) = delete;
    Md5Context& operator=(const Md5Context&) = delete;

    bool valid() const { return valid_; }

    // Hash a file incrementally. Returns hex string or nullopt on error.
    std::optional<std::string> hash_file(const std::string& path);

private:
    bool valid_ = false;
    void* algorithm_ = nullptr;   // BCRYPT_ALG_HANDLE
    void* hash_handle_ = nullptr; // BCRYPT_HASH_HANDLE
    void* hash_obj_ = nullptr;    // PBCRYPT_HASH_OBJECT
    std::vector<uint8_t> hash_obj_storage_;
};

} // namespace file_hashing
