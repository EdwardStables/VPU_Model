#include <iostream>
#include <memory>
#include <iomanip>

#include "memory.h"
#include "config.h"
#include "defs_pkg.h"
#include "manager_core.h"

void initialise_state(vpu::config::Config& config, std::unique_ptr<vpu::mem::Memory>& memory) {
    vpu::mem::Snooper::copy_file_in(memory, config.input_file);
};
    
void dump_program(std::unique_ptr<vpu::mem::Memory>& memory){
    for (int i=0; true; i++) {
        uint32_t data = memory->read(i*4);
        //Region end marker
        if (data == 0xFFFFFFFF) break;
        vpu::defs::Opcode opcode = vpu::defs::get_opcode(data);
        std::cout << std::setfill('0') << std::setw(8) << std::hex << i*4;
        std::cout << " " << vpu::defs::opcode_to_string(opcode) << std::endl;
    }
}

void run_program(vpu::core::ManagerCore& core, vpu::config::Config& config) {
    uint32_t cycle = 0;    
    uint32_t step_count = 1;
    if (config.trace) core.print_trace(cycle);
    while (!core.check_has_halted()) {
        cycle++;
        core.run_cycle();

        if (step_count > 0) step_count--;
        if (config.trace) core.print_trace(cycle);
        if (config.step && step_count == 0){
            std::string step_count_str; 
            std::getline(std::cin, step_count_str);
            if (step_count_str.length() == 0)
                step_count = 0;
            else
                step_count = std::stoi(step_count_str);
        }
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

    vpu::core::ManagerCore core(config, memory);
    run_program(core, config);

    return 0;
}