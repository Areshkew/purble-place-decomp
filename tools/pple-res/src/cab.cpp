#include "pple/cab.hpp"

#include <cstring>

#include "pple/inflate.hpp"

namespace pple {
namespace {

std::uint16_t rd16(std::span<const std::uint8_t> b, std::size_t off) {
    return static_cast<std::uint16_t>(b[off] | (static_cast<std::uint16_t>(b[off + 1]) << 8));
}

std::uint32_t rd32(std::span<const std::uint8_t> b, std::size_t off) {
    return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8) |
           (static_cast<std::uint32_t>(b[off + 2]) << 16) | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

constexpr std::size_t kHeaderSize = 36;

}  // namespace

std::string CabFolder::compression_name() const {
    switch (compression & 0x000F) {
        case 0: return "none";
        case 1: return "MSZIP";
        case 2: return "Quantum";
        case 3: return "LZX";
        default: return "unknown(" + std::to_string(compression) + ")";
    }
}

std::string CabArchive::version_string() const {
    return std::to_string(version_major) + "." + std::to_string(version_minor);
}

bool CabArchive::looks_like_cab(std::span<const std::uint8_t> data) {
    return data.size() >= kHeaderSize && std::memcmp(data.data(), "MSCF\0\0\0\0", 8) == 0;
}

std::optional<CabArchive> CabArchive::parse(std::span<const std::uint8_t> data, std::string& error) {
    if (!looks_like_cab(data)) {
        error = "missing MSCF signature";
        return std::nullopt;
    }
    CabArchive cab;
    cab.cabinet_size = rd32(data, 8);
    const std::uint32_t coff_files = rd32(data, 16);
    cab.version_minor = data[24];
    cab.version_major = data[25];
    const std::uint16_t cfolders = rd16(data, 26);
    const std::uint16_t cfiles = rd16(data, 28);
    cab.flags = rd16(data, 30);
    cab.set_id = rd16(data, 32);
    cab.cabinet_index = rd16(data, 34);

    if (cab.cabinet_size > data.size()) {
        error = "cbCabinet (" + std::to_string(cab.cabinet_size) + ") exceeds available bytes (" +
                std::to_string(data.size()) + ")";
        return std::nullopt;
    }

    const std::size_t folder_dir = kHeaderSize;
    if (folder_dir + static_cast<std::size_t>(cfolders) * 8 > data.size()) {
        error = "folder table truncated";
        return std::nullopt;
    }
    for (std::uint16_t i = 0; i < cfolders; ++i) {
        CabFolder f;
        f.data_offset = rd32(data, folder_dir + i * 8);
        f.block_count = rd16(data, folder_dir + i * 8 + 4);
        f.compression = rd16(data, folder_dir + i * 8 + 6);
        cab.folders.push_back(f);
    }

    if (coff_files >= data.size()) {
        error = "coffFiles out of range";
        return std::nullopt;
    }
    std::size_t p = coff_files;
    for (std::uint16_t i = 0; i < cfiles; ++i) {
        if (p + 16 > data.size()) {
            error = "file entry truncated";
            return std::nullopt;
        }
        CabFileEntry f;
        f.uncompressed_size = rd32(data, p);
        f.folder_offset = rd32(data, p + 4);
        f.folder_index = rd16(data, p + 8);
        f.attributes = rd16(data, p + 14);
        const std::size_t name_begin = p + 16;
        std::size_t name_end = name_begin;
        while (name_end < data.size() && data[name_end] != 0) ++name_end;
        if (name_end >= data.size()) {
            error = "file name not terminated";
            return std::nullopt;
        }
        f.name.assign(reinterpret_cast<const char*>(data.data() + name_begin), name_end - name_begin);
        cab.files.push_back(std::move(f));
        p = name_end + 1;
    }

    for (std::size_t fi = 0; fi < cab.folders.size(); ++fi) {
        const CabFolder& f = cab.folders[fi];
        std::size_t q = f.data_offset;
        for (std::uint16_t bi = 0; bi < f.block_count; ++bi) {
            if (q + 8 > data.size()) {
                error = "CFDATA header out of range";
                return std::nullopt;
            }
            CabBlock blk;
            blk.folder_index = static_cast<std::uint16_t>(fi);
            blk.checksum = rd32(data, q);
            blk.compressed_size = rd16(data, q + 4);
            blk.uncompressed_size = rd16(data, q + 6);
            if (q + 8 + blk.compressed_size > data.size()) {
                error = "CFDATA block out of range";
                return std::nullopt;
            }
            cab.blocks.push_back(blk);
            q += 8 + blk.compressed_size;
        }
    }
    return cab;
}

bool CabArchive::extract_folder(std::span<const std::uint8_t> data, std::uint16_t folder_index,
                                std::vector<std::uint8_t>& out, std::string& error,
                                std::size_t* block_errors) const {
    if (folder_index >= folders.size()) {
        error = "folder index out of range";
        return false;
    }
    const CabFolder& folder = folders[folder_index];
    if ((folder.compression & 0x000F) != 1) {
        error = "unsupported compression: " + folder.compression_name();
        return false;
    }

    out.clear();
    std::size_t q = folder.data_offset;
    std::size_t bad = 0;
    for (std::uint16_t bi = 0; bi < folder.block_count; ++bi) {
        const std::size_t header = q;
        const std::uint16_t csize = rd16(data, header + 4);
        const std::uint16_t usize = rd16(data, header + 6);
        const std::span<const std::uint8_t> payload = data.subspan(header + 8, csize);

        // One MSZIP segment per CFDATA block: "CK" then a raw DEFLATE stream.
        if (payload.size() < 2 || payload[0] != 'C' || payload[1] != 'K') {
            error = "block " + std::to_string(bi) + ": missing CK signature";
            return false;
        }
        const InflateStatus st = inflate_raw(payload.subspan(2), out);
        if (!st.ok) {
            error = "block " + std::to_string(bi) + ": " + st.error;
            return false;
        }
        const std::size_t produced = out.size() - st.out_begin;
        if (produced != usize) {
            ++bad;
            if (bad == 1) {
                error = "block " + std::to_string(bi) + ": produced " + std::to_string(produced) +
                        " bytes, header says " + std::to_string(usize);
            }
        }
        q = header + 8 + csize;
    }
    if (block_errors) *block_errors = bad;
    return bad == 0;
}

bool CabArchive::extract_single(std::span<const std::uint8_t> data, std::vector<std::uint8_t>& out,
                                std::string& error) const {
    if (folders.size() != 1 || files.size() != 1) {
        error = "not a single-folder single-file cabinet (" + std::to_string(folders.size()) +
                " folders, " + std::to_string(files.size()) + " files)";
        return false;
    }
    if (!extract_folder(data, 0, out, error)) return false;
    if (out.size() != files[0].uncompressed_size) {
        error = "inflated " + std::to_string(out.size()) + " bytes, CFFILE says " +
                std::to_string(files[0].uncompressed_size);
        return false;
    }
    return true;
}

}  // namespace pple
