import pytest
from pathlib import Path
from util import RegState
from PIL import Image, ImageFile, ImageChops

def params():
    progs = [
        "blitter_text"
    ]
    return [((p,False,False,True), p, "frames_" + p) for p in progs]

@pytest.mark.parametrize("run_program, reference_images, output_images", params(), indirect=True)
def test_framebuffer_output(run_program, reference_images, output_images):
    assert len(reference_images) == len(output_images), "Mismatch in number of output images"

    print("starting comparison", len(reference_images), "to test")

    for i, ((_, ref), (path, out)) in enumerate(zip(reference_images, output_images)):
        print("testing frame", i)
        diff = ImageChops.difference(ref.convert("RGB"), out.convert("RGB"))
        if diff.getbbox():
            diff.save(path.parent / f"diff_{i:05}.png")
            assert False, f"Images index {i} mismatch"
