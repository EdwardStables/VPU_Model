#pragma once

namespace vpu {

template <typename C>
class Subsystem {
    virtual void run_cycle() = 0;
    virtual bool submit(C command, std::function<void()> completion_callback) = 0;
};

}