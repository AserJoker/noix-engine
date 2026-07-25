#include "debug/Sha1.h"
#include <cstring>

namespace noix::debug {

static inline uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

std::vector<uint8_t> sha1(const std::string& data) {
    // Initialize H0..H4
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    size_t len = data.size();

    // Pre-processing: pad to 512-bit block
    std::vector<uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);

    uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bitLen >> (i * 8)));

    // Process each 512-bit block
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t tmp = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = rotl32(tmp, 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else              { f = b ^ c ^ d;            k = 0xCA62C1D6; }

            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    // Produce digest
    std::vector<uint8_t> digest(20);
    for (int i = 0; i < 4; ++i) {
        digest[i]      = static_cast<uint8_t>(h0 >> (24 - i * 8));
        digest[i + 4]  = static_cast<uint8_t>(h1 >> (24 - i * 8));
        digest[i + 8]  = static_cast<uint8_t>(h2 >> (24 - i * 8));
        digest[i + 12] = static_cast<uint8_t>(h3 >> (24 - i * 8));
        digest[i + 16] = static_cast<uint8_t>(h4 >> (24 - i * 8));
    }
    return digest;
}

std::string sha1Hex(const std::string& data) {
    static const char hex[] = "0123456789abcdef";
    auto digest = sha1(data);
    std::string result;
    result.reserve(digest.size() * 2);
    for (auto b : digest) {
        result.push_back(hex[(b >> 4) & 0x0F]);
        result.push_back(hex[b & 0x0F]);
    }
    return result;
}

} // namespace noix::debug
