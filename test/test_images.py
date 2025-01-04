import pytest
from pathlib import Path
from util import RegState
from PIL import Image, ImageFile, ImageChops

TEST_FILES = [
    "dma_copy",
    "dma_set",
]

def params(prog):
    return [((prog,False,True,True),prog+".png",prog+".png")]

@pytest.mark.parametrize("run_program, reference_image, output_image", params("blitter_text"), indirect=True)
def test_blitter_text(run_program, reference_image, output_image):
    reference_image: ImageFile
    output_image: ImageFile

    diff = ImageChops.difference(reference_image, output_image)

    assert not diff.getbbox(), "Images are different"


