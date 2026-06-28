//
// file_hashing.cpp — MD5 file hashing using Windows CNG (BCrypt) API.
// Uses incremental hashing for efficient streaming of large files.
//

#include "file_hashing.hpp"
#include "string_utils.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

// Link the CNG library
#pragma comment(lib, "bcrypt.lib")

namespace file_hashing {

// ============================================================
// Constants
// ============================================================

static constexpr size_t MD5_HASH_SIZE = 16;
static constexpr size_t HASH_CHUNK_SIZE = 1024 * 1024; // 1 MB

// ============================================================
// Hex conversion
// ============================================================

static std::string to_hex(const uint8_t* data, size_t size) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(hex_chars[data[i] >> 4]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

// ============================================================
// One-shot buffer hashing
// ============================================================

std::string md5_hex(const uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[MD5_HASH_SIZE] = {};

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM,
                                                   nullptr, 0);
    if (status != 0) return {};

    status = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    if (size > 0) {
        status = BCryptHashData(hash, const_cast<PUCHAR>(data),
                                static_cast<ULONG>(size), 0);
        if (status != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return {};
        }
    }

    status = BCryptFinishHash(hash, digest, MD5_HASH_SIZE, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status != 0) return {};
    return to_hex(digest, MD5_HASH_SIZE);
}

std::string md5_hex_string(const std::string& data) {
    return md5_hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

// ============================================================
// File hashing (streaming)
// ============================================================

std::optional<std::string> md5_file(const std::string& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[MD5_HASH_SIZE] = {};

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM,
                                                   nullptr, 0);
    if (status != 0) return std::nullopt;

    status = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::nullopt;
    }

    // Open file in binary mode
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::nullopt;
    }

    std::vector<char> chunk(HASH_CHUNK_SIZE);
    bool ok = true;

    while (file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        auto bytes_read = file.gcount();
        if (bytes_read > 0) {
            status = BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(chunk.data()),
                static_cast<ULONG>(bytes_read), 0);
            if (status != 0) {
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::nullopt;
    }

    status = BCryptFinishHash(hash, digest, MD5_HASH_SIZE, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status != 0) return std::nullopt;
    return to_hex(digest, MD5_HASH_SIZE);
}

// ============================================================
// Md5Context — reusable CNG context for batch hashing
// ============================================================

Md5Context::Md5Context() {
    BCRYPT_ALG_HANDLE alg = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM,
                                                   nullptr, 0);
    if (status != 0) return;

    // Query the hash object size for reusable hash state
    DWORD obj_size = 0;
    DWORD copied = 0;
    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&obj_size), sizeof(obj_size), &copied, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return;
    }

    hash_obj_storage_.resize(obj_size);

    BCRYPT_HASH_HANDLE handle = nullptr;
    status = BCryptCreateHash(alg, &handle, hash_obj_storage_.data(),
        obj_size, nullptr, 0, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return;
    }

    algorithm_ = alg;
    hash_handle_ = handle;
    valid_ = true;
}

Md5Context::~Md5Context() {
    // Note: with a reusable hash object, the hash handle persists.
    // For per-file hashing we re-create the hash state each time.
    // The handle and algorithm are reused across files.
    if (hash_handle_) {
        BCryptDestroyHash(reinterpret_cast<BCRYPT_HASH_HANDLE>(hash_handle_));
    }
    if (algorithm_) {
        BCryptCloseAlgorithmProvider(
            reinterpret_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
    }
}

std::optional<std::string> Md5Context::hash_file(const std::string& path) {
    if (!valid_) return std::nullopt;

    BCRYPT_ALG_HANDLE alg = reinterpret_cast<BCRYPT_ALG_HANDLE>(algorithm_);
    BCRYPT_HASH_HANDLE hash = nullptr;

    // Create fresh hash state using the pre-allocated object buffer
    NTSTATUS status = BCryptCreateHash(alg, &hash,
        hash_obj_storage_.data(),
        static_cast<DWORD>(hash_obj_storage_.size()),
        nullptr, 0, 0);
    if (status != 0) return std::nullopt;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        BCryptDestroyHash(hash);
        return std::nullopt;
    }

    std::vector<char> chunk(HASH_CHUNK_SIZE);
    bool ok = true;

    while (file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        auto bytes_read = file.gcount();
        if (bytes_read > 0) {
            status = BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(chunk.data()),
                static_cast<ULONG>(bytes_read), 0);
            if (status != 0) {
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        BCryptDestroyHash(hash);
        return std::nullopt;
    }

    uint8_t digest[MD5_HASH_SIZE] = {};
    status = BCryptFinishHash(hash, digest, MD5_HASH_SIZE, 0);
    BCryptDestroyHash(hash);

    if (status != 0) return std::nullopt;
    return to_hex(digest, MD5_HASH_SIZE);
}

} // namespace file_hashing
