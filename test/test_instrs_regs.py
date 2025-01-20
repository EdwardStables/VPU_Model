import pytest
from util import RegState, get_param

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
        "nops" :         RegState(0x24,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "branch" :       RegState(0x38, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "inc" :          RegState(0x24, 11,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "jump" :         RegState(0x30, 23,  0, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0),
        "left_shifts" :  RegState(0x38,  4,  2, 4, 0, 0, 0, 0, 0, 2, 0x100000, 0),
        "right_shifts" : RegState(0x58,  4,  1, 4, 1, 4, 0, 0, 0, 1, 0x100000, 0),
    }
    assert prog in expected
    yield expected[prog]

@pytest.mark.parametrize(
    "run_program, actual_registers, expected_registers",
    [(get_param(p,regs=True),p,p) for p in TEST_FILES],
    indirect=True
)
def test_register_state(run_program,actual_registers,expected_registers):
    assert actual_registers == expected_registers

@pytest.mark.parametrize("run_program, actual_memory", [(get_param("store",mem=True),"store")], indirect=True)
def test_store(run_program,actual_memory):
    base = 0x100000
    assert actual_memory[base] == 123
    assert actual_memory[base+1] == 0

@pytest.mark.parametrize("run_program, actual_memory, actual_registers", [(get_param("load",regs=True,mem=True),"load","load")], indirect=True)
def test_load(run_program, actual_memory, actual_registers):
    base = 0x100000
    assert actual_memory[base] == 123
    assert actual_memory[base+4] == 0xFF & 321
    assert actual_memory[base+5] == 0xFF & (321 >> 8)
    assert actual_registers == RegState(0x44, 123, 0x100000, 0, 0, 0, 0, 0, 0, 0, 0x100000, 0)

@pytest.mark.parametrize("run_program, actual_memory, actual_registers", [(get_param("load_store_offsets",regs=True,mem=True),"load_store_offsets","load_store_offsets")], indirect=True)
def test_load_store_offsets(run_program, actual_memory, actual_registers):
    base = 0x100000
    for i in range(10):
        assert actual_memory[base+(4*i)] == i+1

    assert actual_registers == RegState(0x84, 55, 36, 55, 36, 10, 0, 0, 0, 0, 0x100000, 0)