import pytest
from pathlib import Path

TEST_FILES = [
    "nops",
    #"branch",
    #"inc",
    #"jump",
    #"left_shifts",
    #"right_shifts",
]

@pytest.mark.parametrize("run_program, expected_registers", [(p,p) for p in TEST_FILES], indirect=True)
def test_register_state(run_program,expected_registers):
    pass