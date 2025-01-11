#pragma once

namespace vpu {

template <typename C>
class Subsystem {
public:
    enum class State {
        IDLE,
        WORKING,
        FINISHED
    };

    State state = State::IDLE;

    uint32_t work_cycle;
    C working_command;
    std::function<void()> working_callback;
    std::function<void()> finished_callback;
    bool finished_callback_valid = false;

    void base_run_cycle();
    bool base_submit(C command, std::function<void()> completion_callback);
    virtual void run_cycle() = 0;
    virtual bool submit() = 0;
};

template <typename C>
void Subsystem<C>::base_run_cycle() {
    if (finished_callback_valid) {
        finished_callback();
        finished_callback_valid = false;
    }

    if (state == State::IDLE) return;
    if (state == State::FINISHED){ //Finish on the following cycle
        state = State::IDLE;
        return; //may want to rework this to avoid a bubble
    }

    //Cannot start on first cycle
    if (vpu::defs::get_global_cycle() < work_cycle) return;

    run_cycle();

    if (state == State::FINISHED){
        finished_callback = working_callback;
        finished_callback_valid = true;
    }
}

template <typename C>
bool Subsystem<C>::base_submit(C command, std::function<void()> completion_callback) {
    if (state == State::WORKING){ //Can accept input when idle or on last cycle of work
        return false;
    }

    state = State::WORKING; 
    work_cycle = vpu::defs::get_next_global_cycle();
    working_command = command;
    working_callback = completion_callback;
    return submit();
}

}