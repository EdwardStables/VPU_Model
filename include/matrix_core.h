#pragma once

#include <functional>
#include <optional>

#include "defs_pkg.h"
#include "memory.h"
#include "subsystem.h"
#include "cycle_defer.h"

/*
Subsystem providing general purpose 4x4 matrix and 1x4 vector operations, as well
as 3D graphics specific operations.

Supports multiple requestors and can be used for both the CPU and renderer subsystems.
Output buffer fullness used to prevent deadlocks for multiple requestors.

Operates on pointers to output data. Internal data buffering allows outputs to be
in the same location as inputs without overwriting while reading. Pointers are interpreted
as 4x4 matrices (32 bytes) or 4x1 vectors (8 bytes) depending on operation.

All maths are 12.4 fixed point.

Initial implementation is overly optimistic on latency, all operations completing in one cycle.
Therefore would probably be impractical for actual hardware. But this simplifies various things
and can be refined later
*/
namespace vpu::matrix {

enum class Operation {
    NONE,
    //Init
    IDENTITY_MAT,    //Initialise 32 bytes to identity matrix
    SET_MAT,         //Set matrix entry to a value
    SET_VEC,         //Set vector entry to a value
    //Basic operations
    MULT_MAT,        //Multiply two matrices
    MULT_MAT_SCALAR, //Multiply matrix by a scalar
    ADD_MAT,         //Add two matrices
    ADD_MAT_SCALAR,  //Add a scalar to a matrix
    MULT_VEC_SCALAR, //Multiply vector by a scalar
    ADD_VEC,         //Add two vectors 
    ADD_VEC_SCALAR,  //Add a scalar to a vector 
    VEC_MAT_MULT,    //Vector matrix multiply
    //3D specific operations
    ROTATE,          //Multiply matrix by rotation matrix described by vector [X,Y,Z,1], values are factors of pi
    TRANSLATE,       //Multiply matrix by translation matrix described by vector [X,Y,Z,1]
    SCALE,           //Multiply matrix by scale matrix described by vector [X,Y,Z,1]
};

struct Command {
    uint32_t source1_addr;
    uint32_t source2_addr;
    uint32_t dest_addr;
    uint8_t row;
    uint8_t col;
    uint16_t value;
    uint8_t requestor;
    Operation operation = Operation::NONE;
};

using Vec = std::array<uint16_t,4>;
using Mat = std::array<Vec,4>;

//Not used as a frequent data type in the pipeline, but required for certain
//intermediate steps during mathematical operations
using Vec32 = std::array<uint32_t,4>;
using Mat32 = std::array<Vec32,4>;

//We exclusively use unsigned integers in the simulation along with hand-coded
//conversion to best represent that actual hardware working
uint32_t u16_to_u32(uint16_t v);
Mat32 matrix_to_u32(const Mat& mat);

class DataRequestor {
    std::unique_ptr<vpu::mem::Memory>& memory;

    //Get methods return valid when the data is present, handling checking defers etc
    bool requested_matrix1_valid = false;
    bool requested_matrix2_valid = false;
    bool requested_vector1_valid = false;
    bool requested_vector2_valid = false;
    uint32_t matrix1_addr;
    uint32_t matrix2_addr;
    uint32_t vector1_addr;
    uint32_t vector2_addr;
    Defer<Mat> requested_matrix1;
    Defer<Mat> requested_matrix2;
    Defer<Vec> requested_vector1;
    Defer<Vec> requested_vector2;

    void get_matrix_from_memory(uint32_t cycle_offset, bool one);
    void get_vector_from_memory(uint32_t cycle_offset, bool one);
public:
    DataRequestor(std::unique_ptr<vpu::mem::Memory>& memory);
    std::optional<std::pair<Mat,Mat>> get_matrix_pair(uint32_t address1, uint32_t address2);
    std::optional<Mat> get_matrix(uint32_t address);
    void write_matrix(uint32_t address, Mat data);
    std::optional<std::pair<Vec,Vec>> get_vector_pair(uint32_t address1, uint32_t address2);
    std::optional<Vec> get_vector(uint32_t address);
    std::optional<std::pair<Vec,Mat>> get_vector_matrix_pair(uint32_t address1, uint32_t address2);
    void write_vector(uint32_t address, Vec data);

    void invalidate();
};

uint16_t sin_12_4_fp(uint16_t);
std::pair<uint16_t,uint16_t> sin_cos_12_4_fp(uint16_t);
uint16_t ar_shift_right(uint16_t, uint16_t);
uint32_t ar_shift_right_u32(uint32_t, uint32_t);

class Matrix : public Subsystem<Command> {
    std::unique_ptr<vpu::mem::Memory>& memory;
    DataRequestor requestor;

    Mat input_mat1;
    Mat input_mat2;
    Vec input_vec1;
    Vec input_vec2;

    void modify_cycle();

    void vector_cycle();
    bool vector_request_cycle();

    void matrix_cycle();
    bool matrix_request_cycle();
    void matrix_mult_cycle();
    void matrix_elementwise_cycle(bool add);
    void set_mat2_to_rotate();

public:
    Matrix(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual bool submit();
    virtual void run_cycle();
};

}