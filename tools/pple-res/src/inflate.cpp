#include "pple/inflate.hpp"

#include <array>
#include <cstring>

namespace pple {
namespace {

constexpr int kMaxBits = 15;

// LSB-first bit reader over a byte span.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> in) : in_(in) {}

    // Returns -1 when the input is exhausted.
    int bit() {
        if (bit_pos_ >= in_.size() * 8u) return -1;
        const std::size_t byte = bit_pos_ >> 3;
        const int value = (in_[byte] >> (bit_pos_ & 7u)) & 1u;
        ++bit_pos_;
        return value;
    }

    // Reads `n` bits LSB-first.  Returns -1 on exhaustion.
    long bits(int n) {
        long value = 0;
        for (int i = 0; i < n; ++i) {
            const int b = bit();
            if (b < 0) return -1;
            value |= static_cast<long>(b) << i;
        }
        return value;
    }

    void align_to_byte() { bit_pos_ = (bit_pos_ + 7u) & ~std::size_t{7}; }

    std::size_t bytes_consumed() const { return (bit_pos_ + 7u) / 8u; }
    bool overran() const { return bit_pos_ > in_.size() * 8u; }

private:
    std::span<const std::uint8_t> in_;
    std::size_t bit_pos_ = 0;
};

// Canonical Huffman table, decoded with the count/first/index walk from puff.c.
class Huffman {
public:
    // `lengths` holds one code length per symbol; 0 means "symbol not present".
    bool build(const std::uint8_t* lengths, int count) {
        counts_.fill(0);
        symbols_.assign(static_cast<std::size_t>(count), 0);
        for (int i = 0; i < count; ++i) counts_[lengths[i]]++;

        if (counts_[0] == count) return true;  // no codes at all: legal, never used

        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= counts_[len];
            if (left < 0) return false;  // over-subscribed
        }

        std::array<int, kMaxBits + 1> offsets{};
        for (int len = 1; len < kMaxBits; ++len) offsets[len + 1] = offsets[len] + counts_[len];
        for (int symbol = 0; symbol < count; ++symbol) {
            if (lengths[symbol] != 0) symbols_[static_cast<std::size_t>(offsets[lengths[symbol]]++)] = symbol;
        }
        return true;
    }

    // Returns the decoded symbol, or -1 on a bad code / exhausted input.
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            const int b = br.bit();
            if (b < 0) return -1;
            code |= b;
            const int count = counts_[len];
            if (code - count < first) return symbols_[static_cast<std::size_t>(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

private:
    std::array<int, kMaxBits + 1> counts_{};
    std::vector<int> symbols_;
};

constexpr std::array<int, 30> kLengthBase{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258, 0};
constexpr std::array<int, 30> kLengthExtra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0, 0};
constexpr std::array<int, 30> kDistBase{
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistExtra{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool build_fixed(Huffman& lit, Huffman& dist) {
    std::array<std::uint8_t, 288> lit_lengths{};
    for (int i = 0; i < 144; ++i) lit_lengths[i] = 8;
    for (int i = 144; i < 256; ++i) lit_lengths[i] = 9;
    for (int i = 256; i < 280; ++i) lit_lengths[i] = 7;
    for (int i = 280; i < 288; ++i) lit_lengths[i] = 8;
    std::array<std::uint8_t, 30> dist_lengths{};
    dist_lengths.fill(5);
    return lit.build(lit_lengths.data(), 288) && dist.build(dist_lengths.data(), 30);
}

bool build_dynamic(BitReader& br, Huffman& lit, Huffman& dist, std::string& error) {
    const long hlit = br.bits(5);
    const long hdist = br.bits(5);
    const long hclen = br.bits(4);
    if (hlit < 0 || hdist < 0 || hclen < 0) {
        error = "truncated dynamic block header";
        return false;
    }
    const int nlit = static_cast<int>(hlit) + 257;
    const int ndist = static_cast<int>(hdist) + 1;
    const int nclen = static_cast<int>(hclen) + 4;

    static constexpr std::array<int, 19> kOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                                11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::array<std::uint8_t, 19> clen{};
    for (int i = 0; i < nclen; ++i) {
        const long v = br.bits(3);
        if (v < 0) {
            error = "truncated code-length table";
            return false;
        }
        clen[static_cast<std::size_t>(kOrder[static_cast<std::size_t>(i)])] = static_cast<std::uint8_t>(v);
    }
    Huffman clen_huff;
    if (!clen_huff.build(clen.data(), 19)) {
        error = "bad code-length Huffman table";
        return false;
    }

    std::vector<std::uint8_t> lengths(static_cast<std::size_t>(nlit + ndist), 0);
    int i = 0;
    while (i < nlit + ndist) {
        const int symbol = clen_huff.decode(br);
        if (symbol < 0) {
            error = "bad code-length symbol";
            return false;
        }
        if (symbol < 16) {
            lengths[static_cast<std::size_t>(i++)] = static_cast<std::uint8_t>(symbol);
            continue;
        }
        int repeat = 0;
        std::uint8_t value = 0;
        if (symbol == 16) {
            if (i == 0) {
                error = "repeat with no previous code length";
                return false;
            }
            const long extra = br.bits(2);
            if (extra < 0) {
                error = "truncated repeat";
                return false;
            }
            repeat = 3 + static_cast<int>(extra);
            value = lengths[static_cast<std::size_t>(i - 1)];
        } else if (symbol == 17) {
            const long extra = br.bits(3);
            if (extra < 0) {
                error = "truncated repeat";
                return false;
            }
            repeat = 3 + static_cast<int>(extra);
        } else {
            const long extra = br.bits(7);
            if (extra < 0) {
                error = "truncated repeat";
                return false;
            }
            repeat = 11 + static_cast<int>(extra);
        }
        if (i + repeat > nlit + ndist) {
            error = "code-length repeat overruns table";
            return false;
        }
        for (int r = 0; r < repeat; ++r) lengths[static_cast<std::size_t>(i++)] = value;
    }

    if (lengths[256] == 0) {
        error = "no end-of-block code";
        return false;
    }
    if (!lit.build(lengths.data(), nlit)) {
        error = "bad literal/length Huffman table";
        return false;
    }
    if (!dist.build(lengths.data() + nlit, ndist)) {
        error = "bad distance Huffman table";
        return false;
    }
    return true;
}

}  // namespace

InflateStatus inflate_raw(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out) {
    InflateStatus status;
    status.out_begin = out.size();

    BitReader br(in);
    bool final_block = false;

    while (!final_block) {
        const long bfinal = br.bit();
        const long btype = br.bits(2);
        if (bfinal < 0 || btype < 0) {
            status.error = "truncated block header";
            return status;
        }
        final_block = bfinal != 0;

        if (btype == 0) {  // stored
            br.align_to_byte();
            const long len = br.bits(16);
            const long nlen = br.bits(16);
            if (len < 0 || nlen < 0) {
                status.error = "truncated stored block header";
                return status;
            }
            if ((len ^ 0xFFFF) != nlen) {
                status.error = "stored block length check failed";
                return status;
            }
            for (long i = 0; i < len; ++i) {
                const long byte = br.bits(8);
                if (byte < 0) {
                    status.error = "truncated stored block";
                    return status;
                }
                out.push_back(static_cast<std::uint8_t>(byte));
            }
            continue;
        }

        Huffman lit;
        Huffman dist;
        if (btype == 1) {
            if (!build_fixed(lit, dist)) {
                status.error = "failed to build fixed Huffman tables";
                return status;
            }
        } else if (btype == 2) {
            if (!build_dynamic(br, lit, dist, status.error)) return status;
        } else {
            status.error = "reserved block type";
            return status;
        }

        for (;;) {
            const int symbol = lit.decode(br);
            if (symbol < 0) {
                status.error = "bad literal/length symbol";
                return status;
            }
            if (symbol < 256) {
                out.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) break;  // end of block

            const int lendex = symbol - 257;
            if (lendex >= 30) {
                status.error = "invalid length symbol";
                return status;
            }
            const long lextra = br.bits(kLengthExtra[static_cast<std::size_t>(lendex)]);
            if (lextra < 0) {
                status.error = "truncated length extra bits";
                return status;
            }
            const int length = kLengthBase[static_cast<std::size_t>(lendex)] + static_cast<int>(lextra);

            const int dsymbol = dist.decode(br);
            if (dsymbol < 0 || dsymbol >= 30) {
                status.error = "invalid distance symbol";
                return status;
            }
            const long dextra = br.bits(kDistExtra[static_cast<std::size_t>(dsymbol)]);
            if (dextra < 0) {
                status.error = "truncated distance extra bits";
                return status;
            }
            const int distance = kDistBase[static_cast<std::size_t>(dsymbol)] + static_cast<int>(dextra);

            if (distance > static_cast<int>(out.size())) {
                status.error = "distance before start of output";
                return status;
            }
            const std::size_t from = out.size() - static_cast<std::size_t>(distance);
            for (int i = 0; i < length; ++i) out.push_back(out[from + static_cast<std::size_t>(i)]);
        }
    }

    if (br.overran()) {
        status.error = "read past end of input";
        return status;
    }
    status.in_consumed = br.bytes_consumed();
    status.ok = true;
    return status;
}

}  // namespace pple
