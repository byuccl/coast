#!/usr/bin/python3

import sys
import random
import numpy
import pyperclip

BIT_WIDTH = None
TMR = None

clipboard = ""

def printer(s):
	global clipboard

	sys.stdout.write(s)
	clipboard += s

def printMatrix(A, name):
	printer("uint" + str(BIT_WIDTH) + "_t ")
	if TMR:
		printer("__xMR ")
	printer(name + "[side][side] = {")
	
	for x in range(len(A)):
		printer("{" + ",".join(map(str, A[x])) + "}")
		if x != len(A) - 1:
			printer(",")
	
		printer("\n")
	printer("};\n")

def main():
	global BIT_WIDTH
	global TMR
	
	if len(sys.argv) < 5:
		print("Usage: generate.py size seed bitwidth tmr(1/0)")
		return

	size = int(sys.argv[1])
	random.seed(int(sys.argv[2]))
	BIT_WIDTH = int(sys.argv[3])
	TMR = int(sys.argv[4])

	printer("#define	side " + str(size) + "\n\n")

	m1 = [[random.randint(0, 2**BIT_WIDTH-1) for x in range(size)] for y in range(size)]
	printMatrix(m1, "first_matrix")

	m2 = [[random.randint(0, 2**BIT_WIDTH-1) for x in range(size)] for y in range(size)]
	printMatrix(m2, "second_matrix")


	
	m1 = numpy.matrix(m1)
	m2 = numpy.matrix(m2)

	result = (m1 * m2).tolist()

	# print(result)

	# Truncate and compute XOR
	xor = 0
	for x in range(len(result)):
		for y in range(len(result[0])):
			result[x][y] = result[x][y] & (2**BIT_WIDTH-1)
			xor ^= result[x][y]


	# Print golden matrix
	# printMatrix(result, "golden")

	# Compute XOR result
	printer("uint" + str(BIT_WIDTH) + "_t xor_golden = " + str(xor) + ";\n")
	
	pyperclip.copy(clipboard)
	print("Copied to clipboard!")
	
if __name__ == "__main__":
	main()

# int first_matrix[side][side];
# int second_matrix[side][side];