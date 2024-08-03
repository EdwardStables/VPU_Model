import pytest
from pathlib import Path
from util import RegState

TEST_FILES = [
    "dma_copy",
    "dma_set",
]

@pytest.mark.parametrize("run_program", TEST_FILES, indirect=True)
def test_register_state(run_program):
    assert False