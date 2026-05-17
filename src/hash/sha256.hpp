#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PhotoManager::Hash {

class SHA256 {
public:
    static constexpr size_t BLOCK_SIZE  = 64;
    static constexpr size_t DIGEST_SIZE = 32;
    using Digest = std::array<uint8_t, DIGEST_SIZE>;

    SHA256();
    void        update(const uint8_t* data, size_t length);
    void        update(const std::vector<uint8_t>& data);
    Digest      finalize();
    void        reset();

    static Digest   compute(const uint8_t* data, size_t length);
    static std::string toHexString(const Digest& d);
    static Digest   fromHexString(const std::string& hex);

private:
    void transform(const uint8_t* block);

    uint32_t state_[8];
    uint64_t bit_count_;
    uint8_t  buffer_[BLOCK_SIZE];
    size_t   buffer_len_;
    bool     finalized_;
};

} // namespace PhotoManager::Hash