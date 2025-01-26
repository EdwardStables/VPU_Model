#include "matrix_core.h"
#include "defs_pkg.h"
#include <algorithm>
#include <assert.h>
#include <cstdint>
#include <iostream>

namespace vpu::matrix {

DataRequestor::DataRequestor(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory),
      requested_matrix1({}), requested_matrix2({}), 
      requested_vector1({}), requested_vector2({})
{ }

void DataRequestor::get_vector_from_memory(uint32_t cycle_offset, bool one) {
    uint32_t addr = one ? vector1_addr : vector2_addr;
    Defer<Vec>& v = one ? requested_vector1 : requested_vector2;

    uint32_t ret = memory->read_word(addr);
    v.data[0] = ret & 0xFFFF;
    v.data[1] = (ret >> 16) & 0xFFFF;
    ret = memory->read_word(addr+4);
    v.data[2] = ret & 0xFFFF;
    v.data[3] = (ret >> 16) & 0xFFFF;
    v.update(defs::get_global_cycle() + 1);
}

void DataRequestor::get_matrix_from_memory(uint32_t cycle_offset, bool one) {
    uint32_t address = one ? matrix1_addr : matrix2_addr;
    uint32_t head = address & ~uint32_t(0x3C);
    uint32_t offset = address - head;
    auto data = memory->read(head);
    uint32_t have = std::max(uint(32), vpu::defs::MEM_ACCESS_WIDTH - offset); 
    assert(have%2 == 0);

    Defer<Mat>& m = one ? requested_matrix1 : requested_matrix2;
    m.update(defs::get_global_cycle() + 1);
    int row = 0;
    int col = 0;
    
    for (int i = offset; i < vpu::defs::MEM_ACCESS_WIDTH; i+=2) {
        assert(row != 4);

        m.data[row][col] = data[i];
        m.data[row][col] |= data[i+1] << 8;

        col += 1;
        if (col >= 4) {
            col = 0;
            row += 1;
        }
    }

    if (have == 32) return;

    m.update(defs::get_global_cycle() + 2);
    head += vpu::defs::MEM_ACCESS_WIDTH;
    int i = 0;
    while (row < 4) {
        m.data[row][col] = data[i];
        m.data[row][col] |= data[i+1] << 8;
        i += 2;

        col += 1;
        if (col >= 4) {
            col = 0;
            row += 1;
        }
    }
}

std::optional<std::pair<Mat,Mat>> DataRequestor::get_matrix_pair(uint32_t address1, uint32_t address2) {
    //Got the data already
    bool one_ready = requested_matrix1_valid && requested_matrix1.can_run() && matrix1_addr == address1;
    bool two_ready = requested_matrix2_valid && requested_matrix2.can_run() && matrix2_addr == address2;
    if ( one_ready && two_ready) return std::pair{requested_matrix1.data, requested_matrix2.data};

    matrix1_addr = address1;
    matrix2_addr = address2;
    requested_matrix1_valid = true;
    requested_matrix2_valid = true;

    uint32_t cycle_offset = 0 ;
    if (!one_ready) {
        get_matrix_from_memory(cycle_offset, true);
        cycle_offset = defs::get_global_cycle() - requested_matrix1.cycle;
    }
    if (!two_ready) {
        get_matrix_from_memory(cycle_offset, false);
    }
    
    return std::nullopt;
}

std::optional<Mat> DataRequestor::get_matrix(uint32_t address) {
    //Got the data already
    if (requested_matrix1_valid && requested_matrix1.can_run() && matrix1_addr == address)
        return requested_matrix1.data;

    matrix1_addr = address;
    requested_matrix1_valid = true;

    get_matrix_from_memory(0, true);
    
    return std::nullopt;
}

void DataRequestor::write_matrix(uint32_t address, Mat data) {
    uint32_t head = address & ~uint32_t(0x3C);
    uint32_t offset = address - head;
    uint32_t have = std::max(uint(32), vpu::defs::MEM_ACCESS_WIDTH - offset); 

    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> ret;
    uint64_t mask = 0;

    int row = 0;
    int col = 0;
    
    for (int i = offset; i < vpu::defs::MEM_ACCESS_WIDTH; i+=2) {
        assert(row != 4);

        ret[i] = uint8_t(0xFF&data[row][col]);
        ret[i+1] = uint8_t(0xFF&(data[row][col]>>8));
        mask |= (0x1 << i);
        mask |= (0x1 << (i+1));

        col += 1;
        if (col >= 4) {
            col = 0;
            row += 1;
        }
    }

    memory->write_mask(head, ret, mask);

    if (have == 32) return;

    //TODO this is isn't realisitic, doing two full writes per cycle
    ret = {};
    mask = 0;
    head += vpu::defs::MEM_ACCESS_WIDTH;

    int i = 0;
    while (row < 4) {
        ret[i] = uint8_t(0xFF&data[row][col]);
        ret[i+1] = uint8_t(0xFF&(data[row][col]>>8));
        mask |= (0x1 << i);
        mask |= (0x1 << (i+1));

        col += 1;
        if (col >= 4) {
            col = 0;
            row += 1;
        }
    }

    memory->write_mask(head, ret, mask);
}

std::optional<std::pair<Vec,Vec>> DataRequestor::get_vector_pair(uint32_t address1, uint32_t address2) {
    //Got the data already
    bool one_ready = requested_vector1_valid && requested_vector1.can_run() && vector1_addr == address1;
    bool two_ready = requested_vector2_valid && requested_vector2.can_run() && vector2_addr == address2;
    if (one_ready && two_ready) return std::pair{requested_vector1.data, requested_vector2.data};

    vector1_addr = address1;
    vector2_addr = address2;
    requested_vector1_valid = true;
    requested_vector2_valid = true;

    uint32_t cycle_offset = 0 ;
    if (!one_ready) {
        get_vector_from_memory(cycle_offset, true);
        cycle_offset = defs::get_global_cycle() - requested_vector1.cycle;
    }
    if (!two_ready) {
        get_vector_from_memory(cycle_offset, false);
    }
    
    return std::nullopt;   
}

std::optional<Vec> DataRequestor::get_vector(uint32_t address) {
    //Got the data already
    if (requested_vector1_valid && requested_vector1.can_run() && vector1_addr == address)
        return requested_vector1.data;

    vector1_addr = address;
    requested_vector1_valid = true;

    get_vector_from_memory(0, true);
    
    return std::nullopt;
}

void DataRequestor::write_vector(uint32_t address, Vec data) {
    uint32_t word = data[0];
    word |= (data[1] << 16);
    memory->write_word(address, word);
    word = data[2];
    word |= (data[3] << 16);
    memory->write_word(address + 4, word);
}

void Matrix::modify_cycle() {
    assert(working_command.row >= 1 && working_command.row <= 4);
    assert(working_command.col >= 1 && working_command.col <= 4);

    //first two columns in same word, last two in next word
    uint32_t offset = (working_command.col-1) / 2;
    if (working_command.operation == Operation::SET_MAT) {
        offset += (working_command.row-1) * 4 * 2;
    }
    uint32_t address = working_command.source1_addr + offset;

    uint8_t mask;
    uint8_t value;

    if (working_command.col % 2) { //1 and 3 are in lower bytes
        mask = 0x00FF;
        value = working_command.value;
    } else { //2 and 4 are in upper bytes
        mask = 0xFF00;
        value = working_command.value << 16;
    }

    memory->write_word_mask(address, value, mask);
    state = State::FINISHED;
}

bool Matrix::matrix_request_cycle() {
    //Zero the data first
    input_mat1 = {};
    input_mat2 = {};

    uint32_t s1 = working_command.source1_addr;
    uint32_t s2 = working_command.source2_addr;

    if (working_command.operation == Operation::IDENTITY_MAT) { //Requires nothing
        return true;
    }

    if (working_command.operation == Operation::MULT_MAT) { //Requires two matrices
        auto got_data = requestor.get_matrix_pair(s1, s2);
        if (got_data) {
            input_mat1 = got_data.value().first;
            input_mat2 = got_data.value().second;
            return true;
        }
        return false;
    }

    //Remaining require one matrix
    auto got_data = requestor.get_matrix(s1);
    if (got_data) {
        input_mat1 = got_data.value();
        return true;
    }
    return false;
}

void Matrix::matrix_mult_cycle() {
    Mat output = {};
    Mat& i1 = input_mat1;
    Mat& i2 = input_mat2;
    output[0][0] = i1[0][0]*i2[0][0] + i1[0][1]*i2[1][0] + i1[0][2]*i2[2][0] + i1[0][3]*i2[3][0];
    output[0][1] = i1[0][0]*i2[0][1] + i1[0][1]*i2[1][1] + i1[0][2]*i2[2][1] + i1[0][3]*i2[3][1];
    output[0][2] = i1[0][0]*i2[0][2] + i1[0][1]*i2[1][2] + i1[0][2]*i2[2][2] + i1[0][3]*i2[3][2];
    output[0][3] = i1[0][0]*i2[0][3] + i1[0][1]*i2[1][3] + i1[0][2]*i2[2][3] + i1[0][3]*i2[3][3];
    output[1][0] = i1[1][0]*i2[0][0] + i1[1][1]*i2[1][0] + i1[1][2]*i2[2][0] + i1[1][3]*i2[3][0];
    output[1][1] = i1[1][0]*i2[0][1] + i1[1][1]*i2[1][1] + i1[1][2]*i2[2][1] + i1[1][3]*i2[3][1];
    output[1][2] = i1[1][0]*i2[0][2] + i1[1][1]*i2[1][2] + i1[1][2]*i2[2][2] + i1[1][3]*i2[3][2];
    output[1][3] = i1[1][0]*i2[0][3] + i1[1][1]*i2[1][3] + i1[1][2]*i2[2][3] + i1[1][3]*i2[3][3];
    output[2][0] = i1[2][0]*i2[0][0] + i1[2][1]*i2[1][0] + i1[2][2]*i2[2][0] + i1[2][3]*i2[3][0];
    output[2][1] = i1[2][0]*i2[0][1] + i1[2][1]*i2[1][1] + i1[2][2]*i2[2][1] + i1[2][3]*i2[3][1];
    output[2][2] = i1[2][0]*i2[0][2] + i1[2][1]*i2[1][2] + i1[2][2]*i2[2][2] + i1[2][3]*i2[3][2];
    output[2][3] = i1[2][0]*i2[0][3] + i1[2][1]*i2[1][3] + i1[2][2]*i2[2][3] + i1[2][3]*i2[3][3];
    output[3][0] = i1[3][0]*i2[0][0] + i1[3][1]*i2[1][0] + i1[3][2]*i2[2][0] + i1[3][3]*i2[3][0];
    output[3][1] = i1[3][0]*i2[0][1] + i1[3][1]*i2[1][1] + i1[3][2]*i2[2][1] + i1[3][3]*i2[3][1];
    output[3][2] = i1[3][0]*i2[0][2] + i1[3][1]*i2[1][2] + i1[3][2]*i2[2][2] + i1[3][3]*i2[3][2];
    output[3][3] = i1[3][0]*i2[0][3] + i1[3][1]*i2[1][3] + i1[3][2]*i2[2][3] + i1[3][3]*i2[3][3];

    i1 = output;
}

void Matrix::matrix_elementwise_cycle(bool add) {
    if (add) {
        input_mat1[0][0] += input_mat2[0][0];
        input_mat1[0][1] += input_mat2[0][1];
        input_mat1[0][2] += input_mat2[0][2];
        input_mat1[0][3] += input_mat2[0][3];
        input_mat1[1][0] += input_mat2[1][0];
        input_mat1[1][1] += input_mat2[1][1];
        input_mat1[1][2] += input_mat2[1][2];
        input_mat1[1][3] += input_mat2[1][3];
        input_mat1[2][0] += input_mat2[2][0];
        input_mat1[2][1] += input_mat2[2][1];
        input_mat1[2][2] += input_mat2[2][2];
        input_mat1[2][3] += input_mat2[2][3];
        input_mat1[3][0] += input_mat2[3][0];
        input_mat1[3][1] += input_mat2[3][1];
        input_mat1[3][2] += input_mat2[3][2];
        input_mat1[3][3] += input_mat2[3][3];
    } else {
        input_mat1[0][0] *= input_mat2[0][0];
        input_mat1[0][1] *= input_mat2[0][1];
        input_mat1[0][2] *= input_mat2[0][2];
        input_mat1[0][3] *= input_mat2[0][3];
        input_mat1[1][0] *= input_mat2[1][0];
        input_mat1[1][1] *= input_mat2[1][1];
        input_mat1[1][2] *= input_mat2[1][2];
        input_mat1[1][3] *= input_mat2[1][3];
        input_mat1[2][0] *= input_mat2[2][0];
        input_mat1[2][1] *= input_mat2[2][1];
        input_mat1[2][2] *= input_mat2[2][2];
        input_mat1[2][3] *= input_mat2[2][3];
        input_mat1[3][0] *= input_mat2[3][0];
        input_mat1[3][1] *= input_mat2[3][1];
        input_mat1[3][2] *= input_mat2[3][2];
        input_mat1[3][3] *= input_mat2[3][3];
    }
}

void Matrix::matrix_cycle() {
    if (!matrix_request_cycle()) return;

    //Prepare
    Operation operation = working_command.operation;
    uint16_t v = working_command.value;
    switch(operation) {
        case Operation::ROTATE:
        case Operation::TRANSLATE:
        case Operation::SCALE:
            //Set up the rotation/translation/scale matrix in input 2
            std::cerr << "Not implemented yet dummy";
            assert(false);
            break;
        case Operation::ADD_MAT_SCALAR:
            input_mat2[0] = {v, v, v, v}; //Utilise the matrix add logic
            input_mat2[1] = {v, v, v, v};
            input_mat2[2] = {v, v, v, v};
            input_mat2[3] = {v, v, v, v};
            operation = Operation::ADD_MAT;
            break;
        case Operation::IDENTITY_MAT:
            input_mat1[0][0] = 0x10; //0x10 is 1 in 12.4 fixed point
            input_mat1[1][1] = 0x10;
            input_mat1[2][2] = 0x10;
            input_mat1[3][3] = 0x10;
            break;
        case Operation::MULT_MAT_SCALAR:
        case Operation::MULT_MAT:
        case Operation::ADD_MAT:
            break; //nothing necessary, just work on provided inputs
        default:
            std::cerr << "Invalid Matrix matrix result operation ";
            assert(false);
    }

    switch(operation) {
        case Operation::IDENTITY_MAT: //Done via input prep
            break;
        case Operation::MULT_MAT:
        case Operation::ROTATE:
        case Operation::TRANSLATE:
        case Operation::SCALE:
            matrix_mult_cycle();
            break;
        case Operation::MULT_MAT_SCALAR:
        case Operation::ADD_MAT:
            matrix_elementwise_cycle(operation == Operation::ADD_MAT);
            break;
        case Operation::ADD_MAT_SCALAR:
            assert(false); //Shouldn't be present here
        default:
            std::cerr << "Invalid Matrix matrix result operation ";
            assert(false);
    }

    //TODO should take a defered input for more expensive operations
    requestor.write_matrix(working_command.dest_addr, input_mat1);
    state = State::FINISHED;
}

bool Matrix::vector_request_cycle() {
    //Zero the data first
    input_vec1 = {};
    input_vec2 = {};
    input_mat1 = {};

    uint32_t s1 = working_command.source1_addr;
    uint32_t s2 = working_command.source2_addr;

    if ( working_command.operation == Operation::ADD_VEC) { //Requires two vectors
        auto got_data = requestor.get_vector_pair(s1, s2);
        if (got_data) {
            input_vec1 = got_data.value().first;
            input_vec2 = got_data.value().second;
            return true;
        }
        return false;
    } else
    if (
        working_command.operation == Operation::MULT_VEC_SCALAR ||
        working_command.operation == Operation::ADD_VEC_SCALAR
    ) {
        auto got_data = requestor.get_vector(s1);
        if (got_data) {
            input_vec1 = got_data.value();
            return true;
        }
        return false;
    }

    assert(working_command.operation == Operation::VEC_MAT_MULT);
    //Remaining requires a matrix and a vector
    auto got_data = requestor.get_vector_matrix_pair(s1, s2);
    if (got_data) {
        input_vec1 = got_data.value().first;
        input_mat1 = got_data.value().second;
        return true;
    }
    return false;
}

void Matrix::vector_cycle() {
    if (!vector_request_cycle()) return;

    Vec out;
    Vec& iv = input_vec1;
    Mat& im = input_mat1;

    switch(working_command.operation) {
        case Operation::MULT_VEC_SCALAR:
            out[0] = input_vec1[0] * working_command.value;
            out[1] = input_vec1[1] * working_command.value;
            out[2] = input_vec1[2] * working_command.value;
            out[3] = input_vec1[3] * working_command.value;
            break;
        case Operation::ADD_VEC:
            out[0] = input_vec1[0] + input_vec2[0];
            out[1] = input_vec1[1] + input_vec2[1];
            out[2] = input_vec1[2] + input_vec2[2];
            out[3] = input_vec1[3] + input_vec2[3];
            break;
        case Operation::ADD_VEC_SCALAR:
            out[0] = input_vec1[0] + working_command.value;
            out[1] = input_vec1[1] + working_command.value;
            out[2] = input_vec1[2] + working_command.value;
            out[3] = input_vec1[3] + working_command.value;
            break;
        case Operation::VEC_MAT_MULT:
            out[0] = iv[0]*im[0][0] + iv[1]*im[1][0] + iv[2]*im[2][0] + iv[3]*im[3][0];
            out[1] = iv[0]*im[0][1] + iv[1]*im[1][1] + iv[2]*im[2][1] + iv[3]*im[3][1];
            out[2] = iv[0]*im[0][2] + iv[1]*im[1][2] + iv[2]*im[2][2] + iv[3]*im[3][2];
            out[3] = iv[0]*im[0][3] + iv[1]*im[1][3] + iv[2]*im[2][3] + iv[3]*im[3][3];
            break;
        default:
            std::cerr << "Invalid Matrix vector result operation ";
            assert(false);
    }

    requestor.write_vector(working_command.dest_addr, out);
    state = State::FINISHED;
}

void Matrix::run_cycle(){
    switch(working_command.operation) {
        //Modify in-place
        case Operation::SET_MAT:
        case Operation::SET_VEC:
            modify_cycle();
            break;
        //Write new vector
        case Operation::MULT_VEC_SCALAR:
        case Operation::ADD_VEC:
        case Operation::ADD_VEC_SCALAR:
        case Operation::VEC_MAT_MULT:
            vector_cycle();
            break;
        //Write new matrix
        case Operation::IDENTITY_MAT:
        case Operation::MULT_MAT:
        case Operation::MULT_MAT_SCALAR:
        case Operation::ADD_MAT:
        case Operation::ADD_MAT_SCALAR:
        case Operation::ROTATE:
        case Operation::TRANSLATE:
        case Operation::SCALE:
            matrix_cycle();
            break;
        default:
            std::cerr << "Invalid Matrix operation ";
            assert(false);
    }
}

Matrix::Matrix(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory), requestor(memory)
{
}

bool Matrix::submit() {
    assert(working_command.operation != Operation::NONE);
    return true;
}

}
