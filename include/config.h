#include <filesystem>

namespace fs = std::filesystem;

namespace vpu::config {

struct Config {
    fs::path input_file;
    bool dump = false;

    //Check that each item in the config is valid and the combination of them makes sense.
    bool validate();
};

Config parse_arguments(int argc, char *argv[]);

}