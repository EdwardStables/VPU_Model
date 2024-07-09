#include "config.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <tuple>



namespace vpu::config {

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
    std::unordered_map<std::string,std::tuple<std::string,std::string>> positional_arguments = {
        {"program", {"Path to binary input file to run in simulator",""}}
    };

    std::unordered_map<std::string,std::tuple<std::string,std::string,std::string,int,int>> optional_arguments = {
        {"help", {"--help", "-h", "Print this message"                             , 0, 1}},
        {"dump", {"--dump", "-d", "Dump a human readable copy of the input program", 0, 1}}
    };

    bool print_help = false;
    bool error = false;

    for (int i = 1; i < argc; i++){
        bool found = false;
        //optional argument
        if (argv[i][0] == '-'){
            for (auto& [name, attrs] : optional_arguments) {
                auto& [long_arg, short_arg, msg, count, max] = attrs;

                if (argv[i] == long_arg || argv[i] == short_arg){
                    if (count==max){
                        error = true;
                        break;
                    }
                    count++;
                    found=true;
                    break;
                }
            }
            if (error) break;
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
            print_help = true;
            error = true;
            break;
        }
    }

    if (std::get<3>(optional_arguments["help"]) > 0){
        print_help = true;
    }

    if (!print_help && positional_arguments_seen < positional_arguments.size()) {
        std::cerr << "Missing expected argument '" << positional_ordering[positional_arguments_seen] << "'" << std::endl;
        print_help = true;
        error = true;
    }

    if (print_help){
        std::cerr << argv[0] << " ";
        size_t max_len = 0;
        for (auto& name : positional_ordering) {
            max_len = std::max(max_len, name.size());
            std::cerr << name << " ";
        }
        for (auto& [name,attrs] : optional_arguments) {
            auto& [long_arg, short_arg, msg, cnt, limit] = attrs; 
            std::string hint_name = long_arg + "/" + short_arg;
            max_len = std::max(max_len, hint_name.size());
        }
        max_len += 4;
        std::cerr << "<optional flags>" << std::endl;

        std::cerr << "\n" << "Positional Arguments:" << std::endl;

        for (auto& name : positional_ordering) {
            size_t padding = max_len - name.size();
            std::cerr << "    " << name;
            for (int i = 0; i < padding; i++) std::cerr << " ";
            std::cerr << std::get<0>(positional_arguments[name]) << std::endl;
        }

        std::cerr << "\n" << "Optional Arguments:" << std::endl;

        for (auto& [name, attrs] : optional_arguments) {
            auto& [long_arg, short_arg, msg, cnt, limit] = attrs; 
            std::string hint_name = long_arg + "/" + short_arg;
            size_t padding = max_len - hint_name.size();
            std::cerr << "    " << hint_name;
            for (int i = 0; i < padding; i++) std::cerr << " ";
            std::cerr << msg << std::endl;
        }
        exit(error ? 1 : 0);
    }

    if (error){
        exit(1);
    }

    Config config;

    config.input_file = std::get<1>(positional_arguments["program"]);
    config.dump = (bool)std::get<3>(optional_arguments["dump"]);

    return config;
}

}