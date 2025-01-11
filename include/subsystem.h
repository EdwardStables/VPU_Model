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

    virtual void run_cycle() = 0;
    virtual bool submit(C command, std::function<void()> completion_callback) = 0;
};

}