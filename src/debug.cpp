#include <debug.h>
#include <assert.h>
#include <fstream>
#include <iostream>
#include <openssl/md5.h>
#include <optional>

ObjectHash::ObjectHash() {};
ObjectHash::ObjectHash(fs::path path) {
    std::ifstream file(path, std::ios::binary);
    char* partial_read = new char[1024];
    
    MD5_CTX ctx;
    MD5_Init(&ctx);

    while (!file.eof()) {
        file.read(partial_read, 1024);
        auto available_bytes = file.gcount();
        assert(available_bytes >= 0);
        MD5_Update(
            &ctx,
            reinterpret_cast<const unsigned char*>(partial_read),
            (size_t)available_bytes
        );
    }
    delete[] partial_read;

    unsigned char* hash_output = new unsigned char[16];
    MD5_Final(hash_output, &ctx);
    for (int byte = 15; byte >= 0; byte--) {
        if (byte >= 8) {
            upper |= ((uint64_t)(hash_output[15-byte]) << (8*(byte - 8)));
        } else {
            lower |= ((uint64_t)(hash_output[15-byte]) << 8*byte);
        }

    }
    delete[] hash_output;
};

void ObjectHash::set_eight(uint8_t value, uint8_t offset) {
    if (offset >= 8) {
        offset -= 8;
        upper |= ((uint64_t)value) << (8*offset);
    } else {
        lower |= ((uint64_t)value) << (8*offset);
    }
}

bool operator== (const ObjectHash& lhs, const ObjectHash& rhs) {
    return lhs.upper == rhs.upper && lhs.lower == rhs.lower;
}

Debug::Debug(fs::path object_file) : Debug(object_file, fs::path(object_file) += ".dbg") {}

Debug::Debug(fs::path object_file, fs::path debug_file)
    : object_file(object_file), debug_file(debug_file)
{
    if (!fs::exists(object_file)) assert(false); //This shouldn't be possible
    if (!fs::exists(debug_file)) return; //Might not have been generated, or has a non-default name. Let caller handle it

    if (!parse()) return;
    if (!fs::exists(source_file)) return; //We parsed it, but can't find the source file

    valid = true; //Set valid if the files exist and we parse successfully
}

uint32_t Debug::get_field_size(std::ifstream& file) {
    uint32_t field_size;
    file.read(reinterpret_cast<char*>(&field_size), 4);
    return field_size;
}

bool Debug::parse() {
    std::ifstream file(debug_file, std::ios::binary);

    if (!parse_hash(file)) return false;
    if (!parse_path(file)) return false;
    if (!parse_lines(file)) return false;

    return true;
}

bool Debug::parse_hash(std::ifstream& file) {
    uint32_t field_size = get_field_size(file);
    assert(field_size == 16); //Assume 128 bit hash

    char workingchar;
    int offset = 15;
    while (offset >= 0) {
        file.read(&workingchar, 1);
        hash.set_eight(workingchar, offset);
        offset--;
    }

    ObjectHash filehash(object_file);
    if (filehash != hash) {
        std::cout << "Object file hash mismatches stored debug hash" << std::endl;
        std::cout << "File hash is:  " << std::hex << filehash.upper << filehash.lower << std::endl;
        std::cout << "Debug hash is: " << std::hex << hash.upper << hash.lower << std::endl;
        return false;
    }

    std::cout << "Found matching debug file" << std::endl;
    return true;
}

bool Debug::parse_path(std::ifstream& file) {
    uint32_t field_size = get_field_size(file);
    char* temp_path = new char[field_size+1];
    file.read(temp_path, field_size);
    temp_path[field_size] = '\0';
    std::string pathstring(temp_path);
    delete[] temp_path;
    source_file = fs::path(pathstring);

    if (!fs::exists(source_file)) return false;

    return true;
}

bool Debug::parse_lines(std::ifstream& file) {
    std::ifstream source_file_stream(source_file);
    while (!source_file_stream.eof()) {
        std::string l;
        std::getline(source_file_stream, l);
        source_file_contents.push_back(l);
        line_to_pc.push_back(0xFFFFFFFF);
    }
    line_to_pc.push_back(0xFFFFFFFF); //Additional entry for last line

    //How many line mappings do we have
    uint32_t field_size = get_field_size(file);
    //PC index is PC/4
    for(int pc_index = 0; pc_index < field_size; pc_index++) {
        uint32_t line_number;
        file.read(reinterpret_cast<char*>(&line_number), 4);
        pc_to_line.push_back(line_number);
        if (line_number == 0xFFFFFFFF) continue;
        assert(line_number < line_to_pc.size());
        line_to_pc[line_number] = pc_index;
    }

    return true;
}

std::optional<std::reference_wrapper<std::string>> Debug::get_line_at_pc(uint32_t pc) {
    assert((pc & 0x3) == 0);
    uint32_t index = pc_to_line.at(pc >> 2);
    if (index == 0xFFFFFFFF) return std::nullopt;
    return source_file_contents.at(index);
}