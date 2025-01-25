#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <deque>

#include "defs_pkg.h"
#include "subsystem.h"
#include "memory.h"
#include "cycle_defer.h"

/*
The initial rendering system. Takes an address to a single stream object and updates the frame buffer with it.

This accounts for camera position and perspective.

All other features are hard-coded, this is a test system to build upon and will be mostly replaced as development progresses.
Obvious things that are not handled/considered:
- texturing
- lighting
- non-default materials
- clipping
- culling
- depth buffering
*/

/*
Streams are a simple variable length base unit of rendering non-trivial objects. They take in two base voxels, providing
start and end points in world space (the second voxel is assumed to be greater than the first in every dimension). If the two voxels are the same
then a single output voxel is rendered.

The volume defined between these voxels has presence/lack thereof provided via run-length encoding, dimensions incrementing first in
x, then y, then z. Encoding is byte based:

Bit | Meaning
7   | Set for length, unset for mask
6   | For length, set defines voxel presence, unset defines no voxels
5-0 | For length defines the size of the length
6-0 | For mask provides a mask of the next 7 voxels

The initial implementation will not use mask mode.

For example, a 3x3x3 space has 9 voxel positions. If the first two Z layers are entirely filled and the final Z layer only contains a single voxel
in the central position then the following encoding would be used:

Byte  | 0        | 1        | 2        | 3
Value | 11010010 | 10000100 | 11000001 | 10000100

Byte 1 defines the 16 set voxels in the top two Z layers, byte 1 defines the first 4 unset voxels in final Z layer, byte 2 defines the single set voxel in the
final Z layer, and byte 3 defines the final 4 unset voxels.

This could alternatively be represented entirely using the mask format:

Byte  | 0        | 1        | 2        | 3
Value | 01111111 | 01111111 | 01111000 | 00100000

Note that this actually defines 28 voxels, not the 27 in the volume. Excess voxels are ignored.

A more efficient 3-byte packing could be done with a combination of mask and length formats:

Byte  | 0        | 1        | 2       
Value | 11010010 | 00000100 | 00000000
*/

namespace vpu::stream {

struct Voxel {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct StreamByte {
    enum class Type { Length, Mask };
    uint8_t data;
    Type type();
    uint8_t length();
};

//Mimics memory layout, packed in harware
struct Stream {
    Voxel start;
    Voxel end;
    uint32_t byte_count;
    std::vector<StreamByte> stream;
};
const uint32_t VOXEL_BYTES = 12;
const uint32_t STREAM_HEADER_BYTES = (2*VOXEL_BYTES) + 4;
const uint32_t TRANSFORM_BYTES = 16*2;

/*
Stream Renderer:

Once a kick begins each stream renderer accepts up to one input byte per clock.
In principle, multiple stream renderers can work in parallel with defined start
and end positions. However this is not implemented in the initial design

Max rate supports eight voxel renders per cycle, which allows full rate handling
of mask mode. But length mode bytes may take multiple cycles (if set)
*/

enum class Operation {
    NONE,
    RENDER
};

struct Command {
    uint32_t stream_address; //Where to fetch the stream from
    uint32_t start_offset;   //Internal byte offset to render from
    uint32_t end_offset;     //Internal byte offset to stop rendering at (exclusive)
    uint32_t transformation_matrix_address;
    Operation operation = Operation::NONE;
};

struct TransformationCache {
    uint32_t current_address;
    bool valid = false;
    uint16_t mat[4][4];
};

enum class RenderState {
    IDLE,
    DATA_FETCH,
    RENDER,
    DRAIN
};

class StreamRenderer : public Subsystem<Command> {
    Stream active_stream;
    RenderState render_state = RenderState::IDLE;

    //Fetch and render shared variables
    uint32_t memory_request_head = 0;
    bool memory_return_valid = false;
    Defer<std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH>> memory_return;
    std::unique_ptr<vpu::mem::Memory>& memory;

    //Fetch specific variables
    uint32_t request_got_bytes = 0;
    bool stream_fetch_complete = false;
    //Transformation for this object
    TransformationCache transformation;

    //Render specific variables
    StreamByte active_byte;
    uint32_t processed_byte_count = 0;
    uint32_t internal_buffer_offset = 0;
    uint32_t byte_voxel_count = 0;
    Voxel next_to_render;

    using q_entry = std::pair<uint32_t,uint32_t>;
    std::deque<Defer<q_entry>> output_queue;
    //bit of a bodge; account for limited writeback width
    //when we can't unify data and would have to stall pipeline.
    //This value will need a little more refinement
    const int max_queue_len = 32;

    void start_fetch();
    void reset_fetch();
    void start_render();
    void write_queue();

    //TODO: This will need better modeling with a memory interface
    //Currently not considering any kind of subrequestor/interface contention
    void data_fetch_cycle();
    using update_callback = std::function<void(StreamRenderer*,uint32_t,uint8_t)>;
    void data_fetch_cycle_request(const uint32_t base_address, const uint32_t total_bytes, update_callback);
    void update_active_stream(uint32_t header_index, uint8_t byte);
    void update_transform(uint32_t byte_index, uint8_t byte);
    
    void render_cycle();
    void render_cycle_data_fetch();
    void render_cycle_process_byte();
    void render_cycle_submit_voxel();
    void render_cycle_advance(uint8_t count);

public:
    StreamRenderer(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual bool submit();
    virtual void run_cycle();
};

}
