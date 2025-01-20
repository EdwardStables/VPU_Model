from dataclasses import dataclass
@dataclass
class RegState:
    PC: int
    ACC: int
    R1: int
    R2: int
    R3: int
    R4: int
    R5: int
    R6: int
    R7: int
    R8: int
    SP: int
    RA: int

def get_param(prog: str, regs=False, mem=False, framebuffer = False, blob_content = []):
    return (prog, regs, mem, framebuffer, blob_content)
