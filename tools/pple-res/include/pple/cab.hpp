// Microsoft Cabinet (MSCF) reader, limited to what these resource DLLs use:
// one folder, one file, MSZIP compression.
//
// The MSZIP segment inside each CFDATA block is a raw DEFLATE stream prefixed with
// the two bytes "CK".  The 32 KiB history is shared across the blocks of a folder,
// which inflate_raw() reproduces by appending to one output buffer.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct CabFileEntry {
    std::string name;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t folder_offset = 0;
    std::uint16_t folder_index = 0;
    std::uint16_t attributes = 0;
};

struct CabFolder {
    std::uint32_t data_offset = 0;
    std::uint16_t block_count = 0;
    std::uint16_t compression = 0;  // low byte: 0 none, 1 MSZIP, 2 Quantum, 3 LZX
    std::string compression_name() const;
};

struct CabBlock {
    std::uint16_t folder_index = 0;
    std::uint32_t checksum = 0;
    std::uint16_t compressed_size = 0;
    std::uint16_t uncompressed_size = 0;
};

class CabArchive {
public:
    static bool looks_like_cab(std::span<const std::uint8_t> data);
    static std::optional<CabArchive> parse(std::span<const std::uint8_t> data, std::string& error);

    // Inflates every CFDATA block of `folder_index`, carrying the DEFLATE history
    // across blocks.  Verifies each block against its declared cbUncomp.
    bool extract_folder(std::span<const std::uint8_t> data, std::uint16_t folder_index,
                        std::vector<std::uint8_t>& out, std::string& error, std::size_t* block_errors = nullptr) const;

    // Convenience for the 1 folder / 1 file archives used here.
    bool extract_single(std::span<const std::uint8_t> data, std::vector<std::uint8_t>& out,
                        std::string& error) const;

    std::uint32_t cabinet_size = 0;
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;
    std::uint16_t flags = 0;
    std::uint16_t set_id = 0;
    std::uint16_t cabinet_index = 0;
    std::vector<CabFolder> folders;
    std::vector<CabFileEntry> files;
    std::vector<CabBlock> blocks;

    std::string version_string() const;
};

}  // namespace pple
