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
    assert actual_memory[0xF00000-1] == 0
    for offset in range(0x10000):
        assert actual_memory[0xF00000+offset] == 0xFF
    assert actual_memory[0xF10000+1] == 0
