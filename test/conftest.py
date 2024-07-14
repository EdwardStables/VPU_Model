import pytest 
from VPU_ASM.instructions import ISADefinition, load_from_yaml
from VPU_ASM.assembler import Program, write_out
from pathlib import Path
from subprocess import run
from util import RegState

PROGS = Path("VPU_ASM/test_programs")
BINS = Path("test/binaries")
DUMP = Path("test/dumps")
make_args = lambda file: [(PROGS/(file+".asm"),"test/binaries/"+file+".out")]

@pytest.fixture
def isa():
    isa_dict = load_from_yaml("VPU_ASM/instructions.yaml")
    return ISADefinition(isa_dict)

@pytest.fixture
def run_program(isa, request, clean):
    prog = request.param
    inp = PROGS / (prog + ".asm")
    bin = BINS / (prog + ".out")
    dump = DUMP / (prog + ".reg")
    assert inp.exists()

    write_out(Program(inp, isa), Path(bin))
    assert bin.exists()

    proc = run(f"build/vpu {bin} --dump_regs {dump}", timeout=1, shell=True)
    assert proc.returncode == 0
    assert dump.exists()

    yield
    if clean:
        Path(bin).unlink()
        (DUMP/(prog + ".reg")).unlink()

@pytest.fixture
def actual_registers(request):
    prog = request.param
    dump = DUMP / (prog + ".reg")
    v = {}
    with dump.open() as f:
        for line in f:
            reg, val = line.split()
            v[reg] = int(val)
    regstate = RegState(v['ACC'],v['PC'],v['R1'],v['R2'],v['R3'],v['R4'],v['R5'],v['R6'],v['R7'],v['R8'])
    yield regstate

def pytest_addoption(parser):
    parser.addoption("--no_clean", action="store_true")

def pytest_generate_tests(metafunc):
    no_clean = metafunc.config.option.no_clean
    if 'clean' in metafunc.fixturenames:
        metafunc.parametrize("clean",[not no_clean])