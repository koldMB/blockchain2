#include "merkle.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "myhash.h"

namespace bc {

std::string myhash_hex_bytes(const std::vector<uint8_t>& data) {
    const hash256 h = myHash(data.data(), data.size());
    std::ostringstream os;  // toks pat formatas kaip myhash_hex: 8 zodziai po 8 hex simbolius
    os << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) os << std::setw(8) << h.w[i];
    return os.str();
}

std::string bytes_to_hex(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t x : b) {
        s += d[x >> 4];
        s += d[x & 15];
    }
    return s;
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::invalid_argument("nelyginis hex ilgis");
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::invalid_argument("netinkamas hex simbolis");
    };
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>((val(hex[2 * i]) << 4) | val(hex[2 * i + 1]));
    return out;
}

std::string merkle_root(const std::vector<std::string>& leaf_ids) {
    using Bytes = std::vector<uint8_t>;
    if (leaf_ids.empty()) return myhash_hex("");
    std::vector<Bytes> level;
    level.reserve(leaf_ids.size());
    for (const auto& id : leaf_ids) {
        Bytes b = hex_to_bytes(id);
        if (b.size() != 32) throw std::invalid_argument("id turi buti 32 baitai (64 hex)");
        level.push_back(std::move(b));
    }
    while (level.size() > 1) {
        if (level.size() % 2 == 1) level.push_back(level.back());  // neporinis -> su savo kopija
        std::vector<Bytes> next;
        next.reserve(level.size() / 2);
        for (size_t i = 0; i < level.size(); i += 2) {
            Bytes cat = level[i];
            cat.insert(cat.end(), level[i + 1].begin(), level[i + 1].end());
            const hash256 h = myHash(cat.data(), cat.size());
            Bytes out(32);
            for (int k = 0; k < 8; ++k) {  // zodis -> 4 baitai (didziausias pirmas, kaip hex israiskoje)
                out[4 * k] = static_cast<uint8_t>(h.w[k] >> 24);
                out[4 * k + 1] = static_cast<uint8_t>(h.w[k] >> 16);
                out[4 * k + 2] = static_cast<uint8_t>(h.w[k] >> 8);
                out[4 * k + 3] = static_cast<uint8_t>(h.w[k]);
            }
            next.push_back(std::move(out));
        }
        level = std::move(next);
    }
    return bytes_to_hex(level[0]);
}

}  // namespace bc
