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
        "nops" :         RegState(0x1c,  0,  0, 0, 0, 0, 0, 0, 0, 0),
        "branch" :       RegState(0x30, 10, 10, 0, 0, 0, 0, 0, 0, 0),
        "inc" :          RegState(0x1c, 11,  0, 0, 0, 0, 0, 0, 0, 0),
        "jump" :         RegState(0x28, 23,  0, 0, 0, 0, 0, 0, 0, 0),
        "left_shifts" :  RegState(0x30,  4,  2, 4, 0, 0, 0, 0, 0, 2),
        "right_shifts" : RegState(0x50,  4,  1, 4, 1, 4, 0, 0, 0, 1),
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