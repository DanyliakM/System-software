#include "sha256.hpp"
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace PhotoManager::Hash {

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sig0(uint32_t x) { return rotr(x,2)  ^ rotr(x,13) ^ rotr(x,22); }
inline uint32_t sig1(uint32_t x) { return rotr(x,6)  ^ rotr(x,11) ^ rotr(x,25); }
inline uint32_t gam0(uint32_t x) { return rotr(x,7)  ^ rotr(x,18) ^ (x>>3); }
inline uint32_t gam1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x>>10); }

} // anonymous namespace

SHA256::SHA256() { reset(); }

void SHA256::reset() {
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
    bit_count_  = 0;
    buffer_len_ = 0;
    finalized_  = false;
}

void SHA256::transform(const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i*4+0]) << 24) |
               (static_cast<uint32_t>(block[i*4+1]) << 16) |
               (static_cast<uint32_t>(block[i*4+2]) <<  8) |
               (static_cast<uint32_t>(block[i*4+3]));
    }
    for (int i = 16; i < 64; ++i)
        w[i] = gam1(w[i-2]) + w[i-7] + gam0(w[i-15]) + w[i-16];

    uint32_t a=state_[0], b=state_[1], c=state_[2], d=state_[3],
             e=state_[4], f=state_[5], g=state_[6], h=state_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sig1(e) + ch(e,f,g) + K[i] + w[i];
        uint32_t t2 = sig0(a) + maj(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void SHA256::update(const uint8_t* data, size_t length) {
    if (finalized_) throw std::logic_error("SHA256: already finalized");
    bit_count_ += length * 8;
    size_t i = 0;
    if (buffer_len_ > 0) {
        size_t need = BLOCK_SIZE - buffer_len_;
        size_t take = (length < need) ? length : need;
        std::memcpy(buffer_ + buffer_len_, data, take);
        buffer_len_ += take;
        i += take;
        if (buffer_len_ == BLOCK_SIZE) {
            transform(buffer_);
            buffer_len_ = 0;
        }
    }
    while (i + BLOCK_SIZE <= length) {
        transform(data + i);
        i += BLOCK_SIZE;
    }
    if (i < length) {
        std::memcpy(buffer_, data + i, length - i);
        buffer_len_ = length - i;
    }
}

void SHA256::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

SHA256::Digest SHA256::finalize() {
    if (finalized_) throw std::logic_error("SHA256: already finalized");
    finalized_ = true;

    uint64_t bits = bit_count_;
    uint8_t pad = 0x80;
    update(&pad, 1);
    while (buffer_len_ != 56) {
        uint8_t z = 0;
        update(&z, 1);
    }
    for (int i = 7; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>(bits >> (i * 8));
        std::memcpy(buffer_ + buffer_len_, &b, 1);
        buffer_len_++;
    }
    transform(buffer_);

    Digest digest;
    for (int i = 0; i < 8; ++i) {
        digest[i*4+0] = static_cast<uint8_t>(state_[i] >> 24);
        digest[i*4+1] = static_cast<uint8_t>(state_[i] >> 16);
        digest[i*4+2] = static_cast<uint8_t>(state_[i] >>  8);
        digest[i*4+3] = static_cast<uint8_t>(state_[i]);
    }
    return digest;
}

SHA256::Digest SHA256::compute(const uint8_t* data, size_t length) {
    SHA256 h;
    h.update(data, length);
    return h.finalize();
}

std::string SHA256::toHexString(const Digest& d) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto b : d) ss << std::setw(2) << static_cast<int>(b);
    return ss.str();
}

SHA256::Digest SHA256::fromHexString(const std::string& hex) {
    if (hex.size() != DIGEST_SIZE * 2) throw std::invalid_argument("Invalid hex digest length");
    Digest d;
    for (size_t i = 0; i < DIGEST_SIZE; ++i) {
        unsigned int byte;
        std::istringstream ss(hex.substr(i*2, 2));
        ss >> std::hex >> byte;
        d[i] = static_cast<uint8_t>(byte);
    }
    return d;
}

} // namespace PhotoManager::Hash