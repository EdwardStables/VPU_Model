#include <filesystem>

namespace fs = std::filesystem;

struct Config {
    fs::path input_file;

    //Check that each item in the config is valid and the combination of them makes sense.
    bool validate();
};
