import pytest
from pathlib import Path
from util import RegState
from PIL import Image

TEST_FILES = [
    "dma_copy",
    "dma_set",
]

def params(prog):
    return [((prog,False,True,True),prog+".png",prog+".png")]

@pytest.mark.parametrize("run_program, reference_image, output_image", params("blitter_text"), indirect=True)
def test_blitter_text(run_program, reference_image, output_image):
    #Constants that may change with config, currently no easy way to extract them
    pass


