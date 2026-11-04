#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bc {

// pagalbines funkcijos (naudoja myhash.h; paties myhash algoritmas nekeistas)
std::string myhash_hex_bytes(const std::vector<uint8_t>& data);  // myHash(baitai) -> 64 hex simboliai
std::string bytes_to_hex(const std::vector<uint8_t>& b);
std::vector<uint8_t> hex_to_bytes(const std::string& hex);  // tik mazosios hex raides; kitaip std::invalid_argument

// merkle saknis is transakciju id (64 hex simboliai kiekvienas), bloko tvarka.
// lapai = id, dekoduoti i 32 baitus. vidinis mazgas = myhash(kairys_baitai || desinys_baitai).
// neporinis mazgas poruojamas su savo kopija. vienas lapas = saknis (grazinamas pats id).
// tuscias sarasas (genesis) = myhash_hex("") . netinkamas id formatas -> std::invalid_argument.
std::string merkle_root(const std::vector<std::string>& leaf_ids);

}  // namespace bc
