#include <iostream>
#include <memory>

#include "memory.h"
#include "config.h"
#include "defs_pkg.h"

void initialise_state(vpu::config::Config& config, std::unique_ptr<vpu::mem::Memory>& memory) {
    vpu::mem::Snooper::copy_file_in(memory, config.input_file);
};
    
void dump_program(std::unique_ptr<vpu::mem::Memory>& memory){
    for (int i=0; true; i++) {
        uint32_t data = memory->read(i*4);
        //Region end marker
        if (data == 0xFFFFFFFF) break;
        auto instr = vpu::defs::get_instr(data);
        std::cout << std::hex << i*4 << " " << vpu::defs::instr_to_string(instr) << std::endl;
    }
}


int main(int argc, char *argv[]) {
    auto config = vpu::config::parse_arguments(argc, argv);
    if (!config.validate()) {
        exit(1);
    }
    auto memory = std::make_unique<vpu::mem::Memory>(); 
    initialise_state(config, memory);
    

    if (config.dump) {
        dump_program(memory);
        exit(0);
    }

    return 0;
}