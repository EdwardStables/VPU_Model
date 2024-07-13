import pytest
from pathlib import Path

PROGS = Path("VPU_ASM/test_programs")
make_args = lambda file: [(PROGS/(file+".asm"),"test/binaries/"+file+".out")]

@pytest.mark.parametrize("compile_program", make_args("nops"), indirect=True)
def test_nops(compile_program):
    pass