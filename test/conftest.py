import pytest 
from VPU_ASM.instructions import ISADefinition, load_from_yaml
from VPU_ASM.assembler import Program, write_out
from pathlib import Path

@pytest.fixture
def isa():
    isa_dict = load_from_yaml("VPU_ASM/instructions.yaml")
    return ISADefinition(isa_dict)

@pytest.fixture
def compile_program(request, isa, clean):
    asm, out = request.param
    write_out(Program(asm, isa), Path(out))
    yield out
    if clean:
        Path(out).unlink()

def pytest_addoption(parser):
    parser.addoption("--no_clean", action="store_true")

def pytest_generate_tests(metafunc):
    no_clean = metafunc.config.option.no_clean
    if 'clean' in metafunc.fixturenames:
        metafunc.parametrize("clean",[not no_clean])