#pragma once

#include <filesystem>
#include <variant>

namespace fs = std::filesystem;

namespace vpu::config {

struct Config {
    struct PosArg {
        std::string description = "";
        std::string value = "";
    };

    struct OptArg {
        enum class ArgType {
            BOOLEAN,
            STRING,
            //...
        };

        std::string long_name = "";
        std::string short_name = "";
        std::string description = "";
        uint count = 0;
        uint max_count = 1;
        ArgType type = ArgType::BOOLEAN;
        std::variant<bool,std::string> value;
        static OptArg OptBoolean(std::string l, std::string s, std::string d);
        static OptArg OptString(std::string l, std::string s, std::string d);
    };


    fs::path input_file;
    bool dump = false;
    bool trace = false;
    bool step = false;
    std::string dump_regs = "";

    //Check that each item in the config is valid and the combination of them makes sense.
    bool validate();
};

Config parse_arguments(int argc, char *argv[]);

}