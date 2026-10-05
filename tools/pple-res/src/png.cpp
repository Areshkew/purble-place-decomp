#include "pple/png.hpp"

#include <array>
#include <cstring>

#include "pple/inflate.hpp"

namespace pple {
namespace {

constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t o) {
    return (static_cast<std::uint32_t>(b[o]) << 24) | (static_cast<std::uint32_t>(b[o + 1]) << 16) |
           (static_cast<std::uint32_t>(b[o + 2]) << 8) | static_cast<std::uint32_t>(b[o + 3]);
}

int channels_for(int colour_type) {
    switch (colour_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Expands one reconstructed scanline (already at sample granularity, bit depth kept)
// into 8-bit RGBA pixels.
struct ExpandContext {
    int colour_type = 0;
    int bit_depth = 0;
    std::array<std::uint8_t, 768> palette{};  // 256 * RGB
    std::size_t palette_entries = 0;
    std::array<std::uint8_t, 256> trns{};
    bool has_trns = false;
    bool trns_is_colour_key = false;
    std::uint16_t key_r = 0, key_g = 0, key_b = 0;
};

std::uint32_t sample_at(std::span<const std::uint8_t> row, int index, int bit_depth) {
    if (bit_depth == 8) return row[static_cast<std::size_t>(index)];
    if (bit_depth == 16) return (static_cast<std::uint32_t>(row[static_cast<std::size_t>(index) * 2]) << 8) |
                               row[static_cast<std::size_t>(index) * 2 + 1];
    const int per_byte = 8 / bit_depth;
    const int byte_index = index / per_byte;
    const int shift = 8 - bit_depth * ((index % per_byte) + 1);
    const std::uint32_t mask = (1u << bit_depth) - 1u;
    return (row[static_cast<std::size_t>(byte_index)] >> shift) & mask;
}

void scale_to_8(std::uint32_t value, int bit_depth, std::uint8_t& out) {
    switch (bit_depth) {
        case 1: out = value ? 255 : 0; return;
        case 2: out = static_cast<std::uint8_t>(value * 85); return;
        case 4: out = static_cast<std::uint8_t>(value * 17); return;
        case 8: out = static_cast<std::uint8_t>(value); return;
        case 16: out = static_cast<std::uint8_t>(value >> 8); return;
        default: out = 0; return;
    }
}

void expand_row(std::span<const std::uint8_t> row, std::uint32_t width, const ExpandContext& ctx,
                std::uint8_t* dst) {
    const int per_pixel = channels_for(ctx.colour_type);
    for (std::uint32_t x = 0; x < width; ++x) {
        const int first = static_cast<int>(x) * per_pixel;
        std::uint8_t r = 0, g = 0, b = 0, a = 255;
        if (ctx.colour_type == 0 || ctx.colour_type == 4) {
            std::uint8_t grey8 = 0;
            scale_to_8(sample_at(row, first, ctx.bit_depth), ctx.bit_depth, grey8);
            r = g = b = grey8;
            if (ctx.colour_type == 4) {
                std::uint8_t alpha8 = 0;
                scale_to_8(sample_at(row, first + 1, ctx.bit_depth), ctx.bit_depth, alpha8);
                a = alpha8;
            } else if (ctx.has_trns && ctx.trns_is_colour_key) {
                std::uint8_t key8 = 0;
                scale_to_8(ctx.key_r, ctx.bit_depth, key8);
                if (grey8 == key8) a = 0;
            }
        } else if (ctx.colour_type == 3) {
            const int index = static_cast<int>(sample_at(row, first, ctx.bit_depth));
            if (static_cast<std::size_t>(index) < ctx.palette_entries) {
                r = ctx.palette[static_cast<std::size_t>(index) * 3];
                g = ctx.palette[static_cast<std::size_t>(index) * 3 + 1];
                b = ctx.palette[static_cast<std::size_t>(index) * 3 + 2];
            }
            if (ctx.has_trns && index < 256) a = ctx.trns[static_cast<std::size_t>(index)];
        } else {  // 2 or 6
            std::uint8_t rr = 0, gg = 0, bb = 0;
            scale_to_8(sample_at(row, first, ctx.bit_depth), ctx.bit_depth, rr);
            scale_to_8(sample_at(row, first + 1, ctx.bit_depth), ctx.bit_depth, gg);
            scale_to_8(sample_at(row, first + 2, ctx.bit_depth), ctx.bit_depth, bb);
            r = rr;
            g = gg;
            b = bb;
            if (ctx.colour_type == 6) {
                std::uint8_t aa = 0;
                scale_to_8(sample_at(row, first + 3, ctx.bit_depth), ctx.bit_depth, aa);
                a = aa;
            } else if (ctx.has_trns && ctx.trns_is_colour_key) {
                std::uint8_t kr = 0, kg = 0, kb = 0;
                scale_to_8(ctx.key_r, ctx.bit_depth, kr);
                scale_to_8(ctx.key_g, ctx.bit_depth, kg);
                scale_to_8(ctx.key_b, ctx.bit_depth, kb);
                if (r == kr && g == kg && b == kb) a = 0;
            }
        }
        dst[static_cast<std::size_t>(x) * 4 + 0] = r;
        dst[static_cast<std::size_t>(x) * 4 + 1] = g;
        dst[static_cast<std::size_t>(x) * 4 + 2] = b;
        dst[static_cast<std::size_t>(x) * 4 + 3] = a;
    }
}

}  // namespace

std::string PngImage::colour_type_name() const {
    switch (colour_type) {
        case 0: return "grey";
        case 2: return "RGB";
        case 3: return "palette";
        case 4: return "grey+alpha";
        case 6: return "RGBA";
        default: return "unknown";
    }
}

bool png_probe(std::span<const std::uint8_t> data, std::uint32_t& width, std::uint32_t& height,
               int& bit_depth, int& colour_type, bool& interlaced) {
    if (data.size() < 8 + 25 || std::memcmp(data.data(), kSignature.data(), 8) != 0) return false;
    if (std::memcmp(data.data() + 12, "IHDR", 4) != 0) return false;
    width = be32(data, 16);
    height = be32(data, 20);
    bit_depth = data[24];
    colour_type = data[25];
    interlaced = data[28] != 0;
    return true;
}

bool png_decode(std::span<const std::uint8_t> data, PngImage& out, std::string& error) {
    std::uint32_t width = 0, height = 0;
    int bit_depth = 0, colour_type = 0;
    bool interlaced = false;
    if (!png_probe(data, width, height, bit_depth, colour_type, interlaced)) {
        error = "not a PNG or IHDR missing";
        return false;
    }
    if (interlaced) {
        error = "interlaced PNG not supported";
        return false;
    }
    const int channels = channels_for(colour_type);
    if (channels == 0) {
        error = "unsupported colour type " + std::to_string(colour_type);
        return false;
    }
    if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 && bit_depth != 16) {
        error = "unsupported bit depth " + std::to_string(bit_depth);
        return false;
    }
    if (colour_type != 0 && colour_type != 3 && bit_depth < 8) {
        error = "sub-byte bit depth is only valid for grey or palette";
        return false;
    }

    ExpandContext ctx;
    ctx.colour_type = colour_type;
    ctx.bit_depth = bit_depth;
    std::vector<std::uint8_t> idat;
    std::size_t pos = 8;
    bool saw_end = false;
    while (pos + 12 <= data.size()) {
        const std::uint32_t length = be32(data, pos);
        const std::string type(reinterpret_cast<const char*>(data.data() + pos + 4), 4);
        const std::size_t body = pos + 8;
        if (body + length + 4 > data.size()) {
            error = "chunk " + type + " overruns the file";
            return false;
        }
        const std::span<const std::uint8_t> chunk = data.subspan(body, length);
        if (type == "IDAT") {
            idat.insert(idat.end(), chunk.begin(), chunk.end());
        } else if (type == "PLTE") {
            const std::size_t entries = std::min<std::size_t>(length / 3, 256);
            std::memcpy(ctx.palette.data(), chunk.data(), entries * 3);
            ctx.palette_entries = entries;
        } else if (type == "tRNS") {
            ctx.has_trns = true;
            if (colour_type == 3) {
                for (std::size_t i = 0; i < chunk.size() && i < 256; ++i) ctx.trns[i] = chunk[i];
            } else if (colour_type == 0 && chunk.size() >= 2) {
                ctx.trns_is_colour_key = true;
                ctx.key_r = static_cast<std::uint16_t>((chunk[0] << 8) | chunk[1]);
            } else if (colour_type == 2 && chunk.size() >= 6) {
                ctx.trns_is_colour_key = true;
                ctx.key_r = static_cast<std::uint16_t>((chunk[0] << 8) | chunk[1]);
                ctx.key_g = static_cast<std::uint16_t>((chunk[2] << 8) | chunk[3]);
                ctx.key_b = static_cast<std::uint16_t>((chunk[4] << 8) | chunk[5]);
            }
        } else if (type == "IEND") {
            saw_end = true;
            break;
        }
        pos = body + length + 4;
    }
    if (idat.empty()) {
        error = "no IDAT data";
        return false;
    }
    if (!saw_end) error = "IEND missing";  // tolerated, decoded from what is present

    // zlib wrapper: 2-byte header, DEFLATE stream, 4-byte Adler-32 trailer.
    if (idat.size() < 6) {
        error = "zlib stream too short";
        return false;
    }
    if ((idat[0] & 0x0F) != 8) {
        error = "zlib CM is not deflate";
        return false;
    }
    if ((idat[1] & 0x20) != 0) {
        error = "zlib preset dictionary not supported";
        return false;
    }
    if (((static_cast<unsigned>(idat[0]) << 8) | idat[1]) % 31u != 0) {
        error = "zlib header check failed";
        return false;
    }
    std::vector<std::uint8_t> raw;
    const InflateStatus st = inflate_raw(std::span<const std::uint8_t>(idat).subspan(2), raw);
    if (!st.ok) {
        error = "inflate failed: " + st.error;
        return false;
    }

    const std::uint64_t bits_per_pixel = static_cast<std::uint64_t>(bit_depth) * channels;
    const std::size_t stride = static_cast<std::size_t>((width * bits_per_pixel + 7) / 8);
    const std::size_t expected = (stride + 1) * height;
    if (raw.size() < expected) {
        error = "inflated " + std::to_string(raw.size()) + " bytes, expected " + std::to_string(expected);
        return false;
    }

    const std::size_t bpp = std::max<std::size_t>(1, bits_per_pixel / 8);
    out.width = width;
    out.height = height;
    out.bit_depth = bit_depth;
    out.colour_type = colour_type;
    out.has_alpha = colour_type == 4 || colour_type == 6 || ctx.has_trns;
    out.from_palette = colour_type == 3;
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);

    std::vector<std::uint8_t> previous(stride, 0);
    std::vector<std::uint8_t> current(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t base = static_cast<std::size_t>(y) * (stride + 1);
        const int filter = raw[base];
        const std::span<const std::uint8_t> src = std::span<const std::uint8_t>(raw).subspan(base + 1, stride);
        for (std::size_t i = 0; i < stride; ++i) {
            const std::uint8_t left = i >= bpp ? current[i - bpp] : 0;
            const std::uint8_t up = previous[i];
            const std::uint8_t upleft = i >= bpp ? previous[i - bpp] : 0;
            std::uint8_t value = src[i];
            switch (filter) {
                case 0: break;
                case 1: value = static_cast<std::uint8_t>(value + left); break;
                case 2: value = static_cast<std::uint8_t>(value + up); break;
                case 3: value = static_cast<std::uint8_t>(value + ((left + up) >> 1)); break;
                case 4: value = static_cast<std::uint8_t>(value + paeth(left, up, upleft)); break;
                default:
                    error = "unknown scanline filter " + std::to_string(filter) + " on row " + std::to_string(y);
                    return false;
            }
            current[i] = value;
        }
        expand_row(std::span<const std::uint8_t>(current), width, ctx,
                   out.rgba.data() + static_cast<std::size_t>(y) * width * 4);
        previous.swap(current);
        std::fill(current.begin(), current.end(), 0);
    }
    return true;
}

}  // namespace pple
