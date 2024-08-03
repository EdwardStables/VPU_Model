#include <assert.h>

#include "defs_pkg.h"

namespace vpu {

template <typename T>
struct Defer {
    uint32_t cycle;
    T data;

    bool can_run(){
        uint32_t cur = defs::get_global_cycle();
        assert(cycle >= cur);
        return cycle == cur;
    }

    void update(uint32_t new_time) {
        assert(new_time > defs::get_global_cycle());
    }

    void increment() {
        cycle++;
    }

    //Valid next cycle
    Defer(T data) :
        cycle(defs::get_next_global_cycle()), data(data)
    {}

    Defer(T data, uint32_t valid_cycle) : Defer(data)
    {
        update(valid_cycle);
    }
};

}
