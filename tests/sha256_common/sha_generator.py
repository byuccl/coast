#!/usr/bin/python3
import sys
import hashlib
import random
import pyperclip

clipboard = ""
TMR = False

def printer(s):
    global clipboard

    sys.stdout.write(s)
    clipboard += s

def printHashData(bytelist, name, TMRd):
    printer("uint8_t ")
        
    if TMR and TMRd:
        printer("__xMR ")

    printer(name + " [" + str(len(bytelist)) + "] = {")

    i = 0
    for b in bytelist:
        printer(hex(b))
        if i != len(bytelist) - 1:
            printer(",")
        if i % 20 == 0:
            printer("\n\t")
        i += 1

    printer("};\n\n")
    

def main():
    global TMR
    
    if len(sys.argv) < 4:
        print("Usage: generate.py size seed tmr(1/0)")
        return

    size = int(sys.argv[1])
    random.seed(int(sys.argv[2]))	
    TMR = int(sys.argv[3])


    b =  bytearray()    
    
    for _ in range(size):
        b.append(random.randint(0, 255))    

    printer("#define	LEN " + str(size) + "\n\n")
    printHashData(b, "hash_data", TMRd = True)
    
    m = hashlib.sha256(b)

    printHashData(m.digest(), "golden", TMRd = False)

    
    pyperclip.copy(clipboard)
    print("Copied to clipboard!")

if __name__ == "__main__":
    main()