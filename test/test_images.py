import pytest
from pathlib import Path
from util import RegState
from PIL import Image, ImageFile, ImageChops

def params():
    progs = [
        "blitter_text"
    ]
    return [((p,False,False,True),p+".png",p+".png") for p in progs]

@pytest.mark.parametrize("run_program, reference_images, output_images", params(), indirect=True)
def test_framebuffer_output(run_program, reference_images, output_images):
    assert len(reference_images) == len(output_images), "Mismatch in number of output images"

    for i, ref, out in enumerate(zip(reference_images, output_images)):
        diff = ImageChops.difference(ref, out)
        assert not diff.getbbox(), f"Images index {i} mismatch"
