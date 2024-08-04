import pytest
from pathlib import Path
from util import RegState

TEST_FILES = [
    "dma_copy",
    "dma_set",
]

def params(prog):
    return [((prog,False,True),prog)]

@pytest.mark.parametrize("run_program, actual_memory", params("dma_set"), indirect=True)
def test_dma_set(run_program,actual_memory):
    base = 0xF00000
    length = 0x10000
    assert actual_memory[base-1] == 0
    for offset in range(length):
        assert actual_memory[base+offset] == 0xFF
    assert actual_memory[base+length] == 0

@pytest.mark.parametrize("run_program, actual_memory", params("dma_copy"), indirect=True)
def test_dma_copy(run_program,actual_memory):
    base = 0xF00000
    length = 0x10000
    assert actual_memory[base-1] == 0
    for offset in range(length):
        assert actual_memory[base+offset] == 0xFF
    assert actual_memory[base+length] == 0

    base = 0xF70000
    assert actual_memory[base-1] == 0
    for offset in range(length):
        assert actual_memory[base+offset] == 0xFF
    assert actual_memory[base+length] == 0
