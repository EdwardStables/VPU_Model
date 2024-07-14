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