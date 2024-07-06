#include "config.h"
#include <iostream>

bool Config::validate() {
    if (!fs::exists(input_file)) {
        std::cerr << "Provided input program " << input_file << " cannot be found" << std::endl;
        return false;
    }

    return true;
}