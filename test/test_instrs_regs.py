import pytest
from pathlib import Path
from util import RegState

TEST_FILES = [
    "nops",
    "branch",
    #"inc",
    #"jump",
    #"left_shifts",
    #"right_shifts",
]

@pytest.fixture
def expected_registers(request):
    prog = request.param
    expected = {
        "nops" :   RegState( 0, 0x10,  0, 0, 0, 0, 0, 0, 0, 0),
        "branch" : RegState(10, 0x18, 10, 0, 0, 0, 0, 0, 0, 0),
    }
    assert prog in expected
    yield expected[prog]

@pytest.mark.parametrize(
    "run_program, actual_registers, expected_registers",
    [(p,p,p) for p in TEST_FILES],
    indirect=True
)
def test_register_state(run_program,actual_registers,expected_registers):
    assert actual_registers == expected_registers