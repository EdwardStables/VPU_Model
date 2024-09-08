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


@pytest.mark.parametrize("run_program, actual_memory", params("blitter_pixel"), indirect=True)
def test_blit_pix(run_program,actual_memory):
    #Constants that may change with config, currently no easy way to extract them
    FRAMEBUFFER_WIDTH = 300
    FRAMEBUFFER_HEIGHT = 200
    FRAMEBUFFER_PIXEL_BYTES = 4
    FRAMEBUFFER_ADDR = 0x1FFC0000

    for y in range(FRAMEBUFFER_HEIGHT):
        for x in range(FRAMEBUFFER_WIDTH):
            offset = (x * FRAMEBUFFER_PIXEL_BYTES) + (FRAMEBUFFER_WIDTH * FRAMEBUFFER_PIXEL_BYTES * y)
            addr = offset + FRAMEBUFFER_ADDR

            if x == 1 and y == 2:
                assert actual_memory[addr] == 0xFF
                assert actual_memory[addr+1] == 0xFF
                assert actual_memory[addr+2] == 0xFF
                assert actual_memory[addr+3] == 0xFF
            elif x == 150 and y == 175:
                assert actual_memory[addr] == 0xFF
                assert actual_memory[addr+1] == 0x00
                assert actual_memory[addr+2] == 0x00
                assert actual_memory[addr+3] == 0xFF
            else:
                assert actual_memory[addr] == 0x0
                assert actual_memory[addr+1] == 0x0
                assert actual_memory[addr+2] == 0x0
                assert actual_memory[addr+3] == 0x0


@pytest.mark.parametrize("run_program, actual_memory", params("blitter_clear"), indirect=True)
def test_blit_clear(run_program,actual_memory):
    #Constants that may change with config, currently no easy way to extract them
    FRAMEBUFFER_WIDTH = 300
    FRAMEBUFFER_HEIGHT = 200
    FRAMEBUFFER_PIXEL_BYTES = 4
    FRAMEBUFFER_ADDR = 0x1FFC0000

    for y in range(FRAMEBUFFER_HEIGHT):
        for x in range(FRAMEBUFFER_WIDTH):
            offset = (x * FRAMEBUFFER_PIXEL_BYTES) + (FRAMEBUFFER_WIDTH * FRAMEBUFFER_PIXEL_BYTES * y)
            addr = offset + FRAMEBUFFER_ADDR

            assert actual_memory[addr] == 0xFF
            assert actual_memory[addr+1] == 0xFF
            assert actual_memory[addr+2] == 0xFF
            assert actual_memory[addr+3] == 0xFF


