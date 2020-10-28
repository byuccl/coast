# contains ANSI escape codes for printing color

from enum import Enum

class tcolors(Enum):
    BLACK   = '\033[30m'
    RED     = '\033[31m'
    GREEN   = '\033[32m'
    YELLOW  = '\033[33m'
    BLUE    = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN    = '\033[36m'
    WHITE   = '\033[37m'
    NONE    = '\033[0m'

class teffects(Enum):
    BOLD        = '\033[1m'
    ITALIC      = '\033[3m'
    UNDERLINE   = '\033[4m'
    BLINK_SLOW  = '\033[5m'
    BLINK_FAST  = '\033[6m'
    REVERSE     = '\033[7m'

    ITALIC_OFF  = '\033[23m'
    UNDERLINE_OFF = '\033[24m'
    BLINK_OFF   = '\033[25m'
    REVERSE_OFF = '\033[27m'
    ALL_OFF     = '\033[0m'


def colorMsg(msg, color):
    if color not in tcolors:
        return msg
    return "{}{}{}".format(color.value, msg, tcolors.NONE.value)


def colorEffectMsg(msg, color, effect):
    if (color not in tcolors) or (effect not in teffects):
        return msg
    return "{}{}{}{}".format(color.value, effect.value, msg, tcolors.NONE.value)
