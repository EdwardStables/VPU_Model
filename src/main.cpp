#include <iostream>
#include <memory>
#include <iomanip>

#include "memory.h"
#include "config.h"
#include "defs_pkg.h"
#include "manager_core.h"
#include "scheduler.h"
#include "dma.h"

#ifdef RPC
#include "rpc_interface.h"
#include "simulator_rpc.h"
#endif

namespace vpu {

class System {
    config::Config config;
    std::unique_ptr<mem::Memory> memory;
    DMA dma;
    Blitter blitter;
    ManagerCore core;
    Scheduler scheduler;

    #ifdef RPC
    std::unique_ptr<SimulatorRPCInterface> server_interface;
    ServerWrapper server_wrapper; 
    #endif



    void initialise_memory_state() {
        vpu::mem::MemorySnooper::copy_file_in(memory, config.input_file);
    };

    void dump_program(){
        for (int i=0; true; i++) {
            uint32_t data = memory->read_word(i*4);
            //Region end marker
            if (data == 0xFFFFFFFF) break;
            vpu::defs::Opcode opcode = vpu::defs::get_opcode(data);
            std::cout << std::setfill('0') << std::setw(8) << std::hex << i*4;
            std::cout << " " << vpu::defs::opcode_to_string(opcode) << std::endl;
        }
    }

    void dump_mem() {
        fs::path dump_path = config.dump_mem;
        if (fs::exists(dump_path) && fs::is_directory(dump_path)) {
            std::cerr << "Error: Dump path " << config.dump_mem << " is a directory. Give a file name.";
            exit(1);
        }

        std::cout << "Dumping memory state to " << config.dump_mem << std::endl;
        std::ofstream dump(dump_path, std::ios::out | std::ios::binary);
        auto& data = mem::MemorySnooper::get_data(memory);
        dump.write((char*)&data[0], data.size());
    }

    void dump_regs() {
        fs::path dump_path = config.dump_regs;
        if (fs::exists(dump_path) && fs::is_directory(dump_path)) {
            std::cerr << "Error: Dump path " << config.dump_regs << " is a directory. Give a file name.";
            exit(1);
        }

        std::cout << "Dumping register state to " << config.dump_regs << std::endl;
        std::ofstream dump(dump_path, std::ios::out);
        for (uint8_t r = 0; r < vpu::defs::REGISTER_COUNT; r++) {
            dump << vpu::defs::register_to_string((vpu::defs::Register)r) << " ";
            dump << vpu::ManagerCoreSnooper::get_register(core,(vpu::defs::Register)r);
            dump << "\n";
        }
    }

    void run_cycle() {
        core.run_cycle();
        scheduler.run_cycle();
        dma.run_cycle();
        blitter.run_cycle();
    }

public:
    void run_program() {
        uint32_t step_count = 1;
        core.print_status_start();
        while (!core.check_has_halted()) {
            run_cycle();
            vpu::defs::increment_global_cycle();

            if (step_count > 0) step_count--;
            core.print_status(vpu::defs::get_global_cycle());
            if (config.step && step_count == 0){
                std::string step_count_str; 
                std::getline(std::cin, step_count_str);
                if (step_count_str.length() == 0)
                    step_count = 0;
                else
                    step_count = std::stoi(step_count_str);
            }
        }

        if (config.dump_regs != "") {
            dump_regs();
        }

        if (config.dump_mem != "") {
            dump_mem();
        }
    }

    System(config::Config config) :
        config(config),
        memory(std::make_unique<vpu::mem::Memory>()),
        dma(memory),
        blitter(memory),
        scheduler(dma, blitter),
        core(config, memory, scheduler)
#ifdef RPC
        ,server_interface(std::make_unique<rpc::ServerInterface>(memory))
        ,server_wrapper(server_interface)
#endif
    {
        initialise_memory_state();

        if (config.dump) {
            dump_program();
            exit(0);
        }
    }
};

}


int main(int argc, char *argv[]) {
 
    auto config = vpu::config::parse_arguments(argc, argv);
    if (!config.validate()) {
        exit(1);
    }

    vpu::System system(config);
    system.run_program();

    return 0;
}
