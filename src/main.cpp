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
#include "stream_renderer.h"

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
    dma::DMA dma;
    blit::Blitter blitter;
    stream::StreamRenderer renderer;
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
        for (int i=memory->read_word(0); true; i+=4) {
            uint32_t data = memory->read_word(i);
            //Region end marker
            if (data == 0xFFFFFFFF)  {
                std::cout << "\n";
                break;
            }
            vpu::defs::Opcode opcode = vpu::defs::get_opcode(data);
            std::cout << std::setfill('0') << std::setw(8) << std::hex << i;
            std::cout << " ";
            std::cout << std::setfill('0') << std::setw(8) << std::hex << data;
            std::cout << " "  << std::setfill(' ') << std::setw(14)
                      << vpu::defs::opcode_to_string(opcode);

            if (debug.valid) {
                auto line_opt = debug.get_line_at_pc(i);
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
        dma.base_run_cycle();
        blitter.base_run_cycle();
        renderer.base_run_cycle();
    }

    bool wait_for_signal(uint32_t pc) {
#ifndef RPC
        return true;
#else
        if (!config.wait) return true;
        std::cout << "Waiting for external control..." << std::endl;
        server_interface.set_pc(pc);
        while (true) {
            auto cmd = server_interface.get_command();
            if (!cmd) continue;

            //TODO: make actual enums for this
            if (cmd.value() == 2) {
                return false; //STEP
            }
            if (cmd.value() == 1) {
                return true; //RUN
            }
        }
#endif
    }

public:
    void run_program() {
        bool run_not_step = wait_for_signal(ManagerCoreSnooper::get_register(core,vpu::defs::PC));
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

#ifdef RPC
            if (config.inspector && !run_not_step) {
                run_not_step = wait_for_signal(ManagerCoreSnooper::get_register(core,vpu::defs::PC));
            }
#endif
        }

        if (config.dump_regs != "") {
            dump_regs();
        }

        if (config.dump_mem != "") {
            dump_mem();
        }

        wait_for_signal(ManagerCoreSnooper::get_register(core,vpu::defs::PC));
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
        renderer(memory),
        scheduler(dma, blitter, renderer),
        core(this->config, memory, scheduler),
        debug(config.input_file)
#ifdef RPC
        ,server_interface(memory.get(), debug, &core)
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

    return 0;
}
