#include <iostream>
#include <memory>
#include <iomanip>
#include <cstdlib>

#include "memory.h"
#include "config.h"
#include "defs_pkg.h"
#include "manager_core.h"
#include "scheduler.h"
#include "dma.h"

#include "debug.h"

#ifdef RPC
#include "rpc_interface.h"
#include "simulator_rpc.h"
#endif

namespace vpu {

class System {
    config::Config config;
    
    //memory is uniquely owned by the system, but is accessed in many places via
    //raw pointers or references to this
    std::unique_ptr<mem::Memory> memory;
    DMA dma;
    Blitter blitter;
    Scheduler scheduler;
    ManagerCore core;

    Debug debug;

#ifdef RPC
    vpu::rpc::ServerInterface server_interface;
    ServerWrapper server_wrapper; 
#endif

    void initialise_memory_state() {
        vpu::mem::MemorySnooper::copy_file_in(memory.get(), config.input_file);
    };

    void dump_program(){
        for (int i=0; true; i++) {
            uint32_t data = memory->read_word(i*4);
            //Region end marker
            if (data == 0xFFFFFFFF)  {
                std::cout << "\n";
                break;
            }
            vpu::defs::Opcode opcode = vpu::defs::get_opcode(data);
            std::cout << std::setfill('0') << std::setw(8) << std::hex << i*4;
            std::cout << " "  << std::setfill(' ') << std::setw(14)
                      << vpu::defs::opcode_to_string(opcode);

            if (debug.valid) {
                auto line_opt = debug.get_line_at_pc(i*4);
                if (line_opt.has_value()) {
                    std::cout << line_opt.value().get();
                }
            }

            std::cout << "\n";
        }
        std::cout << std::flush;
    }

    void dump_mem() {
        fs::path dump_path = config.dump_mem;
        if (fs::exists(dump_path) && fs::is_directory(dump_path)) {
            std::cerr << "Error: Dump path " << config.dump_mem << " is a directory. Give a file name.";
            exit(1);
        }

        std::cout << "Dumping memory state to " << config.dump_mem << std::endl;
        std::ofstream dump(dump_path, std::ios::out | std::ios::binary);
        auto& data = mem::MemorySnooper::get_data(memory.get());
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
        std::cout << "Executing program." << std::endl;
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

#ifdef RPC
    void wait_for_init() {
        if (!config.inspector) return;
        std::cout << "Waiting for RPC server to startup." << std::endl;
        while (!server_wrapper.is_server_running()) {}
    }
#endif

    void end_stall() {
        if (config.wait) {
            std::cout << "Output stall set, waiting forever. Use ctrl+c to end the simulation" << std::endl;
            while (true) {}
        }
    }

    System(config::Config config) :
        config(config),
        memory(std::make_unique<vpu::mem::Memory>()),
        dma(memory),
        blitter(memory),
        scheduler(dma, blitter),
        core(this->config, memory, scheduler),
        debug(config.input_file)
#ifdef RPC
        ,server_interface(memory.get(), debug)
        ,server_wrapper(config.inspector, &server_interface)
#endif
    {
        initialise_memory_state();

        if (config.dump) {
            dump_program();
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
    if (config.dump) return 0;

#ifdef RPC
    system.wait_for_init();
#endif

    system.run_program();
    system.end_stall();

    return 0;
}
