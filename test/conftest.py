import pytest 
from VPU_ASM.instructions import ISADefinition, load_from_yaml
from VPU_ASM.assembler import Program, Data
from pathlib import Path
from subprocess import run
from util import RegState
from PIL import Image

PROGS = Path("VPU_ASM/test_programs")
IMGS = Path("VPU_ASM/test_images")
BINS = Path("test/binaries")
DUMP = Path("test/dumps")
make_args = lambda file: [(PROGS/(file+".asm"),"test/binaries/"+file+".out")]

@pytest.fixture
def isa():
    isa_dict = load_from_yaml("VPU_ASM/instructions.yaml")
    return ISADefinition(isa_dict)

@pytest.fixture
def run_program(isa, request, clean, release):
    prog, regs, mem, framebuffer, blob_files = request.param
    inp = PROGS / (prog + ".asm")
    bin = BINS / (prog + ".out")
    dump_reg = DUMP / (prog + ".reg")
    dump_mem = DUMP / (prog + ".mem")
    dump_framebuffer = DUMP / ("frames_" + prog)

    assert not dump_framebuffer.exists() or dump_framebuffer.is_dir(), "Frambuffer output location exists but is a file not directory"
    if framebuffer and not dump_framebuffer.exists():
        dump_framebuffer.mkdir()
    assert inp.exists()

    blob_names = []
    blobs = []
    if blob_files:
        blob_names = [DUMP/f"{prog}_{i}.blob" for i in range(len(blob_files))]
        for bn,content in zip(blob_names,blob_files):
            with bn.open("wb") as f:
                f.write(content)
            blobs.append(Data(bn))


    program = Program(inp, isa, blobs)
    program.write_out(Path(bin),False)
    assert bin.exists()

    cmd = f"{'release' if release else 'build'}/vpu {bin}"
    if regs:
        cmd += f" --dump_regs {dump_reg}"
    if mem:
        cmd += f" --dump_mem {dump_mem}"
    if framebuffer:
        cmd += f" --dump_framebuffer {dump_framebuffer}"
    proc = run(cmd, timeout=5, shell=True)

    assert proc.returncode == 0
    if regs:
        assert dump_reg.exists()
    if mem:
        assert dump_mem.exists()
    if framebuffer:
        assert dump_framebuffer.exists() and dump_framebuffer.is_dir()

    yield

    #Remove files
    if clean:
        Path(bin).unlink()
        dump_reg.unlink(missing_ok=True)
        dump_mem.unlink(missing_ok=True)
        for bn in blob_names:
            bn.unlink(missing_ok=True)
    #Remove directories if feature flag enabled
    if clean and framebuffer:
        for file in dump_framebuffer.iterdir():
            assert not file.is_dir(), "Found unexpected directory in framebuffer dump location"
            file.unlink()
        dump_framebuffer.rmdir()

def get_images(path: Path):
    if not path.exists():
        raise FileNotFoundError(f"Couldn't find {path}")
    if not path.is_dir():
        raise Exception(f"Expected image path to give a directory not file {path}")

    files = [f for f in path.iterdir() if f.is_file() and f.name.startswith("frame_") and f.name.endswith(".png")]
    files.sort(key=lambda f: f.name)
    return [(f,Image.open(f)) for f in files]

@pytest.fixture
def reference_images(request):
    return get_images(IMGS/request.param)

@pytest.fixture
def output_images(request):
    return get_images(DUMP/request.param)

@pytest.fixture
def actual_registers(request):
    prog = request.param
    dump = DUMP / (prog + ".reg")
    v = {}
    with dump.open() as f:
        for line in f:
            reg, val = line.split()
            v[reg] = int(val)
    regstate = RegState(v['PC'],v['ACC'],v['R1'],v['R2'],v['R3'],v['R4'],v['R5'],v['R6'],v['R7'],v['R8'],v['SP'],v['RA'])
    yield regstate

@pytest.fixture
def actual_memory(request):
    prog = request.param
    dump = DUMP / (prog + ".mem")
    with dump.open('rb') as f:
        data = f.read()
    yield data


def pytest_addoption(parser):
    parser.addoption("--no_clean", action="store_true")
    parser.addoption("--release", action="store_true")

def pytest_generate_tests(metafunc):
    no_clean = metafunc.config.option.no_clean
    if 'clean' in metafunc.fixturenames:
        metafunc.parametrize("clean",[not no_clean])
    release = metafunc.config.option.release
    if 'release' in metafunc.fixturenames:
        metafunc.parametrize("release",[release])
