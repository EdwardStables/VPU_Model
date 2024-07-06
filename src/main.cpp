#include <iostream>
#include "memory.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <tuple>
#include <vector>


void initialise_state(std::unique_ptr<vpu::mem::Memory>& memory) {
    
};

void parse_arguments(int argc, char *argv[]) {
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
        {"help", {"--help","-h","Print this message",0,1}}
    };

    bool print_help = false;
    bool error = true;

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
            if (positional_arguments_seen >= positional_arguments.size()){
                std::cout << "Unexpected argument " << argv[i] << std::endl;
                print_help=true;
            } else {
                auto& [message,value] = positional_arguments[positional_ordering[positional_arguments_seen]];
                value = argv[i];
            }
        }
        if (!found){
            std::cout << "Unknown argument flag " << argv[i] << std::endl;
            print_help = true;
            break;
        }
    }

    if (std::get<3>(optional_arguments["help"]) > 0){
        print_help = true;
    }

    if (!print_help && positional_arguments_seen < positional_arguments.size()) {
        std::cout << "Missing expected argument '" << positional_ordering[positional_arguments_seen] << "'" << std::endl;
        print_help = true;
    }

    if (print_help){
        std::cout << argv[0] << " ";
        size_t max_len = 0;
        for (auto& name : positional_ordering) {
            max_len = std::max(max_len, name.size());
            std::cout << name << " ";
        }
        for (auto& [name,attrs] : optional_arguments) {
            auto& [long_arg, short_arg, msg, cnt, limit] = attrs; 
            std::string hint_name = long_arg + "/" + short_arg;
            max_len = std::max(max_len, hint_name.size());
        }
        max_len += 4;
        std::cout << "<optional flags>" << std::endl;

        std::cout << "\n" << "Positional Arguments:" << std::endl;

        for (auto& name : positional_ordering) {
            size_t padding = max_len - name.size();
            std::cout << "    " << name;
            for (int i = 0; i < padding; i++) std::cout << " ";
            std::cout << std::get<0>(positional_arguments[name]) << std::endl;
        }

        std::cout << "\n" << "Optional Arguments:" << std::endl;

        for (auto& [name, attrs] : optional_arguments) {
            auto& [long_arg, short_arg, msg, cnt, limit] = attrs; 
            std::string hint_name = long_arg + "/" + short_arg;
            size_t padding = max_len - hint_name.size();
            std::cout << "    " << hint_name;
            for (int i = 0; i < padding; i++) std::cout << " ";
            std::cout << msg << std::endl;
        }
        exit(error ? 1 : 0);
    }

    if (error){
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);

    auto memory = std::make_unique<vpu::mem::Memory>(); 
    memory->write(1234, 0xDEADBEEF);
    std::cout << std::hex << memory->read(1234) << std::endl;
    return 0;
}