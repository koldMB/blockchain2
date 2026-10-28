#include "myhash.h"

#include <iomanip>
#include <sstream>

namespace {

inline uint32_t rotl32(uint32_t x, int n) noexcept {
    return (x << n) | (x >> ((32 - n) & 31));
}

inline uint32_t mix32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

constexpr uint32_t GOLDEN = 0x9e3779b9U;
constexpr uint32_t PRIME1 = 0x85ebca6bU;
constexpr uint32_t PRIME2 = 0xc2b2ae35U;

constexpr uint32_t IV[8] = {
    0x6a09e667U ^ 0x01U,
    0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

}

hash256 myHash(const uint8_t* data, size_t len) noexcept {
    uint32_t s[8];

    for (int i = 0; i < 8; ++i)
        s[i] = IV[i] ^ (GOLDEN * static_cast<uint32_t>(i + 1));

    const uint64_t bits = static_cast<uint64_t>(len) * 8u;
    s[0] ^= static_cast<uint32_t>(len);
    s[1] ^= static_cast<uint32_t>(static_cast<uint64_t>(len) >> 32);
    s[2] ^= static_cast<uint32_t>(bits);
    s[3] ^= static_cast<uint32_t>(bits >> 32);

    for (size_t i = 0; i < len; ++i) {
        const uint32_t b    = data[i];
        const uint32_t idx  = static_cast<uint32_t>(i);
        const int      slot = static_cast<int>(i & 7u);

        s[slot]            = mix32(s[slot] ^ (b + GOLDEN * (idx + 1u)));
        s[(slot + 3) & 7] += b + PRIME1 * idx;
        s[(slot + 5) & 7] ^= mix32(b + PRIME2 * (idx + 1u));
    }

    {
        const uint32_t idx  = static_cast<uint32_t>(len);
        const int      slot = static_cast<int>(len & 7u);
        constexpr uint32_t pad = 0x80u;

        s[slot]            = mix32(s[slot] ^ (pad + GOLDEN * (idx + 1u)));
        s[(slot + 3) & 7] += pad + PRIME1 * idx;
        s[(slot + 5) & 7] ^= mix32(pad + PRIME2 * (idx + 1u));
    }

    for (int r = 0; r < 16; ++r) {
        const uint32_t rc = GOLDEN * static_cast<uint32_t>(r + 1);

        for (int i = 0; i < 8; ++i) {
            s[i]            = mix32(s[i] + s[(i + 1) & 7] + rc);
            s[(i + 3) & 7] ^= s[i];
            s[(i + 5) & 7] += rotl32(s[i], (i * 3 + 5) & 31);
        }

        const uint32_t t = s[0];
        for (int i = 0; i < 7; ++i) s[i] = s[i + 1];
        s[7] = t;
    }

    hash256 out{};
    for (int i = 0; i < 8; ++i) out.w[i] = s[i];
    return out;
}

static std::string toHex(const hash256& h) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i)
        os << std::setw(8) << h.w[i];
    return os.str();
}

std::string myhash_hex(const std::string& data) {
    const hash256 h = myHash(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return toHex(h);
}