#pragma once
#include <string>
#include <filesystem>
#include <bitset>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

struct ObjectHash {
    uint64_t upper = 0;
    uint64_t lower = 0;
    void set_eight(uint8_t value, uint8_t offset);
    ObjectHash();
    ObjectHash(fs::path path);
    friend bool operator== (const ObjectHash& lhs, const ObjectHash& rhs);
};


class Debug {
private:
    const fs::path object_file;
    const fs::path debug_file;

    ObjectHash hash;

    //For larger inputs later this will need to be rethought, but this
    //is convenient for now
    fs::path source_file;
    std::vector<std::string> source_file_contents;

    //PC/4 gives index, 0xFFFFFFFF for a PC without a corresponding line
    std::vector<uint32_t> pc_to_line;
    //direct line index, 0xFFFFFFFF for a line without a PC (whitespace etc)
    std::vector<uint32_t> line_to_pc;

public:
    bool valid = false;
    
private:
    bool parse();
    bool parse_hash(std::ifstream& file);
    bool parse_path(std::ifstream& file);
    bool parse_lines(std::ifstream& file);
    uint32_t get_field_size(std::ifstream& file);

public:
    Debug(fs::path object_file);
    Debug(fs::path object_file, fs::path debug_file);

    std::optional<std::reference_wrapper<std::string>> get_line_at_pc(uint32_t pc);
};