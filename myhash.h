#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// 256-bit hash, rev 2.0. 64 lowercase hex chars.
struct hash256 { uint32_t w[8]; };

hash256 myHash(const uint8_t* data, size_t len) noexcept;
std::string myhash_hex(const std::string& data);