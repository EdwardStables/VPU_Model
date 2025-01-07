import pytest
from pathlib import Path
from util import RegState

TEST_FILES = [
    "nops",
    "branch",
    "inc",
    "jump",
    "left_shifts",
    "right_shifts",
]

@pytest.fixture
def expected_registers(request):
    prog = request.param
    expected = {
        "nops" :         RegState(0x1c,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "branch" :       RegState(0x30, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "inc" :          RegState(0x1c, 11,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "jump" :         RegState(0x28, 23,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "left_shifts" :  RegState(0x30,  4,  2, 4, 0, 0, 0, 0, 0, 2, 0x100000, 0),
        "right_shifts" : RegState(0x50,  4,  1, 4, 1, 4, 0, 0, 0, 1, 0x100000, 0),
    }
    assert prog in expected
    yield expected[prog]

@pytest.mark.parametrize(
    "run_program, actual_registers, expected_registers",
    [((p,True,False,False),p,p) for p in TEST_FILES],
    indirect=True
)
def test_register_state(run_program,actual_registers,expected_registers):
    assert actual_registers == expected_registers

@pytest.mark.parametrize("run_program, actual_memory", [(("store",False,True,False),"store")], indirect=True)
def test_store(run_program,actual_memory):
    base = 0x100000
    assert actual_memory[base] == 123
    assert actual_memory[base+1] == 0

@pytest.mark.parametrize("run_program, actual_memory, actual_registers", [(("load",True,True,False),"load","load")], indirect=True)
def test_load(run_program, actual_memory, actual_registers):
    base = 0x100000
    assert actual_memory[base] == 123
    assert actual_memory[base+4] == 0xFF & 321
    assert actual_memory[base+5] == 0xFF & (321 >> 8)
    assert actual_registers == RegState(0x3c, 123, 0x100000, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0)

@pytest.mark.parametrize("run_program, actual_memory, actual_registers", [(("load_store_offsets",True,True,False),"load_store_offsets","load_store_offsets")], indirect=True)
def test_load(run_program, actual_memory, actual_registers):
    base = 0x100000
    for i in range(10):
        assert actual_memory[base+(4*i)] == i+1

    assert actual_registers == RegState(0x7c, 55, 36, 55, 36, 10, 0, 0, 0, 0, 0x100000, 0)