#include "config.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <tuple>
#include <assert.h>



namespace vpu::config {

Config::OptArg Config::OptArg::OptBoolean(std::string l, std::string s, std::string d) {
    OptArg arg;
    arg.long_name = l;
    arg.short_name = s;
    arg.description = d;
    arg.count = 0;
    arg.max_count = 1;
    arg.type = ArgType::BOOLEAN;
    arg.value = false;
    return arg;
}

Config::OptArg Config::OptArg::OptString(std::string l, std::string s, std::string d) {
    OptArg arg;
    arg.long_name = l;
    arg.short_name = s;
    arg.description = d;
    arg.count = 0;
    arg.max_count = 1;
    arg.type = ArgType::STRING;
    arg.value = "";
    return arg;
}

bool Config::validate() {
    if (!fs::exists(input_file)) {
        std::cerr << "Provided input program " << input_file << " cannot be found" << std::endl;
        return false;
    }

    return true;
}

Config parse_arguments(int argc, char *argv[]) {
    if (argc == 1) {
        std::cout << "Error: simulator requires at least one argument. Use --help to see details." << std::endl;
        exit(1);
    }

    int positional_arguments_seen = 0;
    std::vector<std::string> positional_ordering = {"program"};
    std::unordered_map<std::string,Config::PosArg> positional_arguments = {
        {"program", {"Path to binary input file to run in simulator",""}},
    };

    std::unordered_map<std::string,Config::OptArg> optional_arguments = {
        {"help",      Config::OptArg::OptBoolean("--help",      "-h", "Print this message")},
        {"dump",      Config::OptArg::OptBoolean("--dump",      "-d", "Dump a human readable copy of the input program")},
        {"trace",     Config::OptArg::OptBoolean("--trace",     "-t", "Print core state each clock")},
        {"step",      Config::OptArg::OptBoolean("--step",      "-s", "Step a specific number of instructions")},
        {"dump_regs", Config::OptArg::OptString("--dump_regs", "-r", "Dump the register state in a file after completion")},
    };

    bool print_help = false;
    bool error = false;

    bool expecting_optional = false;
    std::string optional_value_target;

    for (int i = 1; i < argc; i++){
        bool found = false;
        //optional argument
        if (argv[i][0] == '-'){
            if (expecting_optional) {
                std::cerr << "Unexpected argument " << argv[i] << ". Was expection a value after  " << argv[i-1] << std::endl;
                error = true;
            }
            for (auto& [name, attrs] : optional_arguments) {
                if (argv[i] == attrs.long_name || argv[i] == attrs.short_name){
                    if (attrs.count==attrs.max_count){
                        std::cerr << "Unexpected argument " << argv[i] << ". Expected at most " << attrs.max_count << std::endl;
                        error = true;
                        break;
                    }
                    attrs.count++;

                    switch (attrs.type) {
                        case Config::OptArg::ArgType::BOOLEAN:
                            attrs.value = true;
                            break;
                        case Config::OptArg::ArgType::STRING:
                            expecting_optional = true;
                            optional_value_target = name;
                            break;
                    }

                    found=true;
                    break;
                }
            }
            if (error) break;
        } else
        if (expecting_optional) {
            found = true;
            expecting_optional = false;
            assert(optional_arguments.count(optional_value_target) == 1);
            assert(optional_arguments[optional_value_target].type == Config::OptArg::ArgType::STRING);
            optional_arguments[optional_value_target].value = argv[i];
        } else {
            found = true;
            if (positional_arguments_seen >= positional_arguments.size()){
                std::cerr << "Unexpected argument " << argv[i] << std::endl;
                print_help=true;
                error = true;
            } else {
                auto& [message,value] = positional_arguments[positional_ordering[positional_arguments_seen]];
                value = argv[i];
                positional_arguments_seen++;
            }
        }
        if (!found){
            std::cerr << "Unknown argument flag " << argv[i] << std::endl;
            error = true;
            break;
        }
    }

    if (expecting_optional) {
        std::cerr << "Unexpected end to arguments. Was expection a value after  " << argv[argc-1] << std::endl;
        error = true;
    }

    if (optional_arguments["help"].count > 0){
        print_help = true;
    }

    if (!print_help && positional_arguments_seen < positional_arguments.size()) {
        std::cerr << "Missing expected argument '" << positional_ordering[positional_arguments_seen] << "'" << std::endl;
        error = true;
    }

    if (print_help || error){
        std::cerr << argv[0] << " ";
        size_t max_len = 0;
        for (auto& name : positional_ordering) {
            max_len = std::max(max_len, name.size());
            std::cerr << name << " ";
        }
        for (auto& [name,attrs] : optional_arguments) {
            std::string hint_name = attrs.long_name + "/" + attrs.short_name;
            max_len = std::max(max_len, hint_name.size());
        }
        max_len += 4;
        std::cerr << "<optional flags>" << std::endl;

        std::cerr << "\n" << "Positional Arguments:" << std::endl;

        for (auto& name : positional_ordering) {
            size_t padding = max_len - name.size();
            std::cerr << "    " << name;
            for (int i = 0; i < padding; i++) std::cerr << " ";
            std::cerr << positional_arguments[name].description << std::endl;
        }

        std::cerr << "\n" << "Optional Arguments:" << std::endl;

        for (auto& [name, attrs] : optional_arguments) {
            std::string hint_name = attrs.long_name + "/" + attrs.short_name;
            size_t padding = max_len - hint_name.size();
            std::cerr << "    " << hint_name;
            for (int i = 0; i < padding; i++) std::cerr << " ";
            std::cerr << attrs.description << std::endl;
        }
        exit(error ? 1 : 0);
    }

    if (error){
        exit(1);
    }

    Config config;

    config.input_file = positional_arguments["program"].value;
    config.dump = std::get<bool>(optional_arguments["dump"].value);
    config.trace = std::get<bool>(optional_arguments["trace"].value);
    config.step = std::get<bool>(optional_arguments["step"].value);
    config.dump_regs = std::get<std::string>(optional_arguments["dump_regs"].value);

    return config;
}

}