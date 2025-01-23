import pytest
from util import get_param

def blob_data():
    return bytes.fromhex("000000010000000200000003000000040000000500000006000000070000000800000009")

def params(prog):
    return [(get_param(prog,mem=True,blob_content=[blob_data()]),prog)]

@pytest.mark.parametrize("run_program, actual_memory", params("blob_load_and_read"), indirect=True)
def test_blob_load_and_read(run_program,actual_memory):
    base = 0x100000
    assert actual_memory[base+3+0] == 1
    assert actual_memory[base+3+1*4] == 2
    assert actual_memory[base+3+2*4] == 3
    assert actual_memory[base+3+3*4] == 4
    assert actual_memory[base+3+4*4] == 5
    assert actual_memory[base+3+5*4] == 6
    assert actual_memory[base+3+6*4] == 7
    assert actual_memory[base+3+7*4] == 8
    assert actual_memory[base+3+8*4] == 9
    assert actual_memory[base+3+9*4] == 0