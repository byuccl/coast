from enum import Enum

class RiscvRegister(Enum):
    """This class is obsolete because we are no longer using Renode.

    It may be possible to support RISC-V in the future, but that
    would require some work to make it work with Qemu.
    """
    # Can't write to register 0 because it is not writable
    # ZERO        = 0
    RA          = 1
    SP          = 2
    GP          = 3
    TP          = 4
    FP          = 8
    PC          = 32
    SSTATUS     = 321
    SIE         = 325
    STVEC       = 326
    SSCRATCH    = 385
    SEPC        = 386
    SCAUSE      = 387
    STVAL       = 388
    SIP         = 389
    MSTATUS     = 833
    MISA        = 834
    MEDELEG     = 835
    MIDELEG     = 836
    MIE         = 837
    MTVEC       = 838
    MSCRATCH    = 897
    MEPC        = 898
    MCAUSE      = 899
    MTVAL       = 900
    MIP         = 901
    PRIV        = 4161
    # The XN registers are the generic names for the 32 basic registers
    #  in the RISC-V architecture.  We leave them out because their
    #  specialized names are more informative, and to avoid giving too much
    #  weight to a specific set of registers over others.
    # X0          = 0
    # X1          = 1
    # X2          = 2
    # X3          = 3
    # X4          = 4
    # X5          = 5
    # X6          = 6
    # X7          = 7
    # X8          = 8
    # X9          = 9
    # X10         = 10
    # X11         = 11
    # X12         = 12
    # X13         = 13
    # X14         = 14
    # X15         = 15
    # X16         = 16
    # X17         = 17
    # X18         = 18
    # X19         = 19
    # X20         = 20
    # X21         = 21
    # X22         = 22
    # X23         = 23
    # X24         = 24
    # X25         = 25
    # X26         = 26
    # X27         = 27
    # X28         = 28
    # X29         = 29
    # X30         = 30
    # X31         = 31
    T0          = 5
    T1          = 6
    T2          = 7
    T3          = 28
    T4          = 29
    T5          = 30
    T6          = 31
    S0          = 8
    S1          = 9
    S2          = 18
    S3          = 19
    S4          = 20
    S5          = 21
    S6          = 22
    S7          = 23
    S8          = 24
    S9          = 25
    S10         = 26
    S11         = 27
    A0          = 10
    A1          = 11
    A2          = 12
    A3          = 13
    A4          = 14
    A5          = 15
    A6          = 16
    A7          = 17
    F0          = 33
    F1          = 34
    F2          = 35
    F3          = 36
    F4          = 37
    F5          = 38
    F6          = 39
    F7          = 40
    F8          = 41
    F9          = 42
    F10         = 43
    F11         = 44
    F12         = 45
    F13         = 46
    F14         = 47
    F15         = 48
    F16         = 49
    F17         = 50
    F18         = 51
    F19         = 52
    F20         = 53
    F21         = 54
    F22         = 55
    F23         = 56
    F24         = 57
    F25         = 58
    F26         = 59
    F27         = 60
    F28         = 61
    F29         = 62
    F30         = 63
    F31         = 64


class A9Register(Enum):
    """Represents the register file in an ARM Cortex-A9 processor."""
    sp          = 13
    lr          = 14
    pc          = 15
    cpsr        = 25
    r0          = 0
    r1          = 1
    r2          = 2
    r3          = 3
    r4          = 4
    r5          = 5
    r6          = 6
    r7          = 7
    r8          = 8
    r9          = 9
    r10         = 10
    r11         = 11
    r12         = 12
    # R13         = 13
    # R14         = 14
    # R15         = 15
    # also some floating point registers
    fpscr       = 16
    fpsid       = 17
    fpexc       = 18
    s0          = 32
    s1          = 33
    s2          = 34
    s3          = 35
    s4          = 36
    s5          = 37
    s6          = 38
    s7          = 39
    s8          = 40
    s9          = 41
    s10         = 42
    s11         = 43
    s12         = 44
    s13         = 45
    s14         = 46
    s15         = 47
    s16         = 48
    s17         = 49
    s18         = 50
    s19         = 51
    s20         = 52
    s21         = 53
    s22         = 54
    s23         = 55
    s24         = 56
    s25         = 57
    s26         = 58
    s27         = 59
    s28         = 60
    s29         = 61
    s30         = 62
    s31         = 63


# must specify which class of register to search
def nameLookup(cls, regStr):
    for r in cls:
        if r.name == regStr:
            return r
    return None
