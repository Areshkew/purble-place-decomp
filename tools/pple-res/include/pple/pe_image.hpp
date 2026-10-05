// Minimal PE reader: image headers plus the full resource tree.
//
// Only what a resource container needs: DOS/NT headers, the section table for
// RVA->file-offset translation, and the three-level resource directory, with both
// named entries (UTF-16) and integer ids.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pple {

struct ResourceEntry {
    std::string type;          // named type, or "#<id>" for an integer type
    std::string name;          // named entry, or "#<id>"
    bool name_is_string = false;
    std::uint32_t language = 0;
    std::uint32_t data_rva = 0;
    std::uint32_t size = 0;
    std::uint32_t file_offset = 0;  // data_rva translated, for convenience
    std::string norm;               // lower-cased, '/' folded to '\\'
};

struct Section {
    std::string name;
    std::uint32_t virtual_size = 0;
    std::uint32_t virtual_address = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t raw_offset = 0;
    std::uint32_t characteristics = 0;
};

class PeImage {
public:
    static std::optional<PeImage> load(const std::string& path, std::string& error);

    const std::vector<ResourceEntry>& resources() const { return resources_; }
    const std::vector<Section>& sections() const { return sections_; }
    std::span<const std::uint8_t> file_bytes() const { return bytes_; }

    std::uint16_t machine() const { return machine_; }
    std::uint16_t subsystem() const { return subsystem_; }
    std::uint16_t characteristics() const { return characteristics_; }
    std::uint32_t entry_point_rva() const { return entry_point_rva_; }
    std::uint64_t image_base() const { return image_base_; }
    std::uint32_t timestamp() const { return timestamp_; }
    bool is_dll() const { return (characteristics_ & 0x2000u) != 0; }
    bool has_code_section() const;

    std::vector<std::uint8_t> read(const ResourceEntry& entry) const;
    const ResourceEntry* find(const std::string& resource_name) const;
    std::vector<std::pair<std::string, std::size_t>> type_counts() const;
    std::string codeview_pdb() const;  // empty when absent

    // Translates an RVA to a file offset.  Returns false when unmapped.
    bool rva_to_offset(std::uint32_t rva, std::uint32_t& offset) const;

private:
    void parse_resources(std::string& error);

    std::vector<std::uint8_t> storage_;
    std::span<const std::uint8_t> bytes_;
    std::vector<ResourceEntry> resources_;
    std::vector<Section> sections_;
    std::uint16_t machine_ = 0;
    std::uint16_t subsystem_ = 0;
    std::uint16_t characteristics_ = 0;
    std::uint32_t timestamp_ = 0;
    std::uint32_t entry_point_rva_ = 0;
    std::uint32_t resource_rva_ = 0;
    std::uint32_t resource_size_ = 0;
    std::uint32_t debug_rva_ = 0;
    std::uint32_t debug_size_ = 0;
    std::uint64_t image_base_ = 0;
};

}  // namespace pple
