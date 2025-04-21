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
Obvious things that are not yet handled/considered:
- texturing
- lighting
- non-default materials
- clipping
- culling
- depth buffering
*/

/*
Streams are a simple variable length base unit of rendering non-trivial objects. They consist of a header structure defining bounds and
a colour table, and then a run-length encoding of voxels information within the bounds.

Format:
Field           | Length            | Usage
----------------|-------------------|-------------------------------------
Start           | 3*32              | Origin voxel position. Usually 0.
End             | 3*32              | End voxel position. Must be greater than Start in all dimensions
Colour Count    | 32                | The number of colour table entries. 0 is an empty table where all voxels are white. The maximal allowed value is 128, 32 bits here for alignment and decoding ease
Stream Size     | 32                | The number of bytes that make up the stream
Colour Table    | 24*Colour Count   | Tightly packed RGB values 
Stream          | 8*Stream Size     | The actual voxel data

The volume defined between the start/end voxels has presence/lack thereof provided via run-length encoding, dimensions incrementing first in
x, then y, then z. Encoding is byte based with each entry made up of a group of one or more bytes. The MSB is set for the final byte in a group.
This allows for extension with additional formatting in future. Current data bytes are:

- Length byte - Defines a 6-bit length and a voxel presence value
- Colour byte - Defines a 7-bit index into the colour table

Length byte:
Bit | Meaning
----|----------------------------------------------------------------------
7   | Set when this is the last byte in a sequence, otherwise interpret the following byte as a colour index
6   | Set defines voxel presence, unset defines no voxels
5-0 | Defines the size of the length

Colour byte:
Bit | Meaning
----|----------------------------------------------------------------------
7   | Set when this is the last byte in a sequence, for current format this should always be set
6-0 | Defines colour table index

For example, a 3x3x3 space has 9 voxel positions. If the first two Z layers are entirely filled and the final Z layer only contains a single voxel
in the central position then the following encoding would be used, with no colour values set:

Byte  | 0        | 1        | 2        | 3
Value | 11010010 | 10000100 | 11000001 | 10000100

Byte 1 defines the 16 set voxels in the top two Z layers, byte 1 defines the first 4 unset voxels in final Z layer, byte 2 defines the single set voxel in the
final Z layer, and byte 3 defines the final 4 unset voxels. As no colour information is used this will take index zero in the colour table.

The same positions could set a different colour for the entire stream:
Byte  | 0        | 1        | 2        | 3        | 4
Value | 01010010 | 10000001 | 10000100 | 11000001 | 10000100

Which would set all voxels to use the colour defined at colour table entry 0. Bytes 0 and 1 are a single group. Bytes 2, 3, and 4 
are each their own group.

*/

namespace vpu::stream {

struct Voxel {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct Colour {
    uint8_t R;
    uint8_t G;
    uint8_t B;
};

struct StreamByte {
    uint8_t data;
    //Is the terminal bit set
    bool terminal();
    //Bits 0-7
    uint8_t value();

    //Length 
    uint8_t length_size();
    bool length_presence();

    //Colour
    uint8_t colour_index();
};

struct ColourTable {
    static const uint32_t COLOUR_SIZE = 3; //each colour is 3 bytes
    static const uint32_t CACHE_SIZE = 4;
    static const uint32_t PLRU_SIZE = 3;

    bool cache_valid = false;
    uint32_t table_addr = 0;
    uint32_t size = 0;

    ColourTable(std::unique_ptr<vpu::mem::Memory>& memory);
    bool read_cache(uint32_t index, uint32_t& colour);
    void initialise(uint32_t addr);
    void invalidate();

private:
    std::unique_ptr<vpu::mem::Memory>& memory;

    //Fetch and render shared variables
    uint32_t memory_request_head = 0;
    bool memory_return_valid = false;
    Defer<std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH>> memory_return;

    std::array<uint32_t,CACHE_SIZE> cache_address;
    std::array<Colour,CACHE_SIZE> cache_data;
    std::array<bool,CACHE_SIZE> presence = {false,false,false,false};
    std::array<bool,PLRU_SIZE> plru = {false,false,false};

    //Return the next way to use and automatically update the plru
    uint8_t update_plru();
};

struct Stream {
    Voxel start;
    Voxel end;
    uint32_t byte_count;
    ColourTable colour_table;
    std::vector<StreamByte> stream;

    Stream(std::unique_ptr<vpu::mem::Memory>& memory);
};

const uint32_t VOXEL_BYTES = 12;
const uint32_t COLOUR_TABLE_SIZE_BYTES = 4;
const uint32_t STREAM_SIZE_BYTES = 4;
const uint32_t STREAM_HEADER_BYTES = (2*VOXEL_BYTES) + COLOUR_TABLE_SIZE_BYTES + STREAM_SIZE_BYTES;
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
    uint32_t colour_table_addr;
    uint8_t colour_table_size;

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
    bool process_sequence = false;
    uint8_t sequence_byte = 0;
    uint32_t processed_byte_count = 0;
    uint32_t internal_buffer_offset = 0;

    uint32_t byte_voxel_count = 0;
    uint8_t colour_index = 0;
    bool draw_voxel = false;

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
    void render_cycle_submit_voxel(uint32_t colour);
    void render_cycle_advance(uint8_t count);

public:
    StreamRenderer(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual bool submit();
    virtual void run_cycle();
};

}
