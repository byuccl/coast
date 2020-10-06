#!/usr/bin/python3

"""Parses data from fault injection runs stored in JSON files."""

import os
import sys
import json
import argparse
import statistics as stats
from datetime import timedelta

import matplotlib.pyplot as plt

import resources.elfUtils as elfUtils
from resources.utils import reverseFormatTime, centerText
from resources.supportClasses import (  InjectionLog,
										RunResult,
										InvalidResult,
										TimeoutResult,
										AbortResult,
										StackOverflowResult,
										AssertionFailResult)


class FileSummary(object):
	"""docstring for FileSummary."""
	def __init__(self, n, l, s, e, f, t, i, cv):
		self.name = n
		self.length = l
		self.success = s
		self.errors = e
		self.faults = f
		self.timeouts = t
		self.invalids = i
		self.totalTime = 0
		self.singleTime = 0
		self.traps = 0
		self.singleAvg = 0
		self.cacheValids = cv
		self.cacheFlag = False
		# optional information
		self.aborts = 0
		self.stackoverflow = 0
		self.assertfail = 0

	def __str__(self):
		Summary = "\n\r" + centerText("Summary for file {}:".format(self.name)) + "\n\r"
		Summary += "Total runs: {}\n".format(self.length)
		successNum = self.success + self.faults
		Summary += "Successes:  {} ({:3.2f}%)\n".format(
				successNum, (successNum/self.length)*100)
		Summary += "Errors:     {} ({:3.2f}%)\n".format(self.errors,
				(self.errors/self.length)*100)
		Summary += "Faults:     {} ({:3.2f}%)\n".format(self.faults,
				(self.faults/self.length)*100)
		Summary += "Timeouts:   {} ({:3.2f}%)\n".format(self.timeouts,
				(self.timeouts/self.length)*100)
		Summary += "Invalid:    {} ({:3.2f}%)\n".format(self.invalids,
				(self.invalids/self.length)*100)
		Summary += "Time to run: " + str(self.totalTime).split('.')[0] + "\n"
		Summary += " ({:3.3f} seconds per injection)\n".format(self.singleTime)
		if (self.aborts > 0) or (self.stackoverflow > 0) or (self.assertfail > 0):
			Summary += "Additional Data:\n"
		if self.aborts > 0:
			Summary += "Aborts:     {} ({:3.2f}%)\n".format(self.aborts,
				(self.aborts/self.length)*100)
		if self.stackoverflow > 0:
			Summary += "Stack OF:   {} ({:3.2f}%)\n".format(self.stackoverflow,
				(self.stackoverflow/self.length)*100)
		if self.assertfail > 0:
			Summary += "AssertFail: {} ({:3.2f}%)\n".format(self.assertfail,
				(self.assertfail/self.length)*100)
		if self.singleAvg > 0:
			Summary += "Average reported run time: {:3.4f}".format(
				self.singleAvg)
		return Summary



def parseCommandLine():
	"""Get configuration options from the command line."""
	parser = argparse.ArgumentParser(description="Read fault injection log data from JSON files")
	parser.add_argument('filename', type=str, help="path to file to read from")
	parser.add_argument('--cycleStats', '-c', help="Information about the cycle counts from each run", action='store_true')
	parser.add_argument('--noSummary', '-n', help="Do not print summary information", action='store_true', default=False)
	parser.add_argument('--countTrap', '-t', help="Count how many of the timeouts were traps", action='store_true')
	parser.add_argument('--parse-dir', '-p', help="parse all log files in a single directory", action='store_true')
	parser.add_argument('--compare-files', '-k', type=str, help="Compare the stats from the first file to the second", metavar='FILE')
	parser.add_argument('--compare-dirs', '-d', type=str, help="Compare the contents of two directories. The [filename] argument is interpreted as the first directory.", metavar='DIR')

	# niche debug output
	parser.add_argument('--register-errors', '-r', action='store_true', help="Report how many injections into each register caused an error.")
	parser.add_argument('--examine-error-addresses', type=str, metavar="BIN_PATH", help="Path to objdump appropriate for the target architecture.")
	parser.add_argument('--symbol-injection-count', type=str, metavar="BIN_PATH", help="Path to objdump appropriate for the target architecture.")

	args = parser.parse_args()
	# validate the file paths
	if not (args.compare_dirs or args.parse_dir or args.register_errors or args.examine_error_addresses or args.symbol_injection_count) \
			and not os.path.isfile(os.path.realpath(args.filename)):
		print("Error, file {} does not exist!".format(args.filename))
		sys.exit(-1)
	if args.compare_files and \
			(not os.path.isfile(os.path.realpath(args.compare_files))):
		print("Error, file {} does not exist!".format(args.compare_files))
		sys.exit(-1)
	if args.compare_dirs:
		if not os.path.isdir(os.path.realpath(args.compare_dirs)):
			print("Error, directory {} does not exist!".format(args.filename))
			sys.exit(-1)
		elif not os.path.isdir(os.path.realpath(args.filename)):
			print("Error, directory {} does not exist!".format(args.compare_files))
			sys.exit(-1)
	if (args.parse_dir or args.register_errors):
		if not os.path.isdir(os.path.realpath(args.filename)):
			print("Error, directory {} does not exist!".format(args.filename))
			sys.exit(-1)

	return args


def readJsonFile(fname):
	objs = []

	with open(fname) as f:
		# first line of every file is the name of the executable used
		execName = f.readline().strip()
		if not os.path.exists(execName):
			print("JSON file {}".format(fname))
			print("   contains invalid file path {}".format(execName))
			sys.exit(1)
		try:
			data = json.load(f)
		except json.decoder.JSONDecodeError:
			print(fname)
			raise

		for d in data:
			try:
				nextData = InjectionLog.FromDict(d)
			except KeyError:
				print(fname)
				print(d)
				raise
			objs.append(nextData)
	return objs, execName


def summarizeRuns(runs, name):
	# counters
	success = 0
	errors = 0
	faults = 0
	timeouts = 0
	invalids = 0
	cacheValids = 0
	# optional data
	aborts = 0
	stackoverflows = 0
	assertfails = 0

	# summarize the data
	for run in runs:
		if isinstance(run.result, InvalidResult):
			invalids += 1
		elif isinstance(run.result, TimeoutResult):
			timeouts += 1
		elif isinstance(run.result, AbortResult):
			# also counts as a timeout, because we get that message too
			timeouts += 1
			aborts += 1
		elif isinstance(run.result, StackOverflowResult):
			timeouts += 1
			stackoverflows += 1
		elif isinstance(run.result, RunResult):
			if run.result.errors > 0:
				errors += 1
			elif run.result.faults > 0:
				faults += 1
			else:
				success += 1
		elif isinstance(run.result, AssertionFailResult):
			errors += 1
			assertfails += 1
		else:
			print("Unclassified!")
			print(run)
		if run.cacheInfo is not None:
			if not run.cacheInfo.dirty:
				cacheValids += 1

	length = len(runs)
	summary = FileSummary(name, length, success, errors, faults, timeouts, invalids, cacheValids)

	# add additional data
	summary.aborts = aborts
	summary.stackoverflow = stackoverflows
	summary.assertfail = assertfails

	if run.cacheInfo is not None:
		summary.cacheFlag = True
	return summary


def summarizeTiming(runs, sum0):
	# how long did it take to run?
	startTime = reverseFormatTime(runs[0].injectionTime)
	endTime = reverseFormatTime(runs[-1].injectionTime)
	tDiff = endTime - startTime
	sum0.totalTime = tDiff

	# time per injection
	tpi = tDiff.total_seconds() / len(runs)
	sum0.singleTime = tpi


def pcStats(runs):
	# information about where the PC was
	# extract the PC values and cycle counts
	info = []
	for r in runs:
		info.append(r.cycles)
		# info.append((r.cycles, r.pcVal))

	# plot it
	n, bins, patches = plt.hist(x=info, bins='auto')
	plt.grid(axis='y')
	plt.xlabel('Cycle count')
	plt.ylabel('Frequency')
	plt.title("Cycle count frequency")
	plt.waitforbuttonpress()


def otherStats(runs, sum0):
	ov_count = 0
	tr_count = 0
	ex_time = []

	for r in runs:
		rr = r.result
		if isinstance(rr, RunResult):
			if rr.isSuccess():
				ex_time.append(rr.runTime)
		elif isinstance(rr, TimeoutResult):
			injTime = reverseFormatTime(r.injectionTime)
			rTime = reverseFormatTime(rr.ftime)
			# print("Timeout = " + str(rTime - injTime))
			# ex_time.append()
		elif isinstance(rr, InvalidResult):
			pass
			# print("Not yet implemented")

		if isinstance(r, TimeoutResult) and r.trap:
			tr_count += 1

	sum0.traps = tr_count
	sum0.singleAvg = stats.mean(ex_time)


def countRegErrors(d0, args):
	sum0, runs = parseOneDir(d0, keepRuns=True)
	# each item will be a list:
	#  [number of injections into register, number of errors, other]
	regMap = {'all' : [0, 0, 0]}

	for run in runs:
		if run.address not in regMap:
			regMap[run.address] = [0, 0, 0]
		# countable results
		if isinstance(run.result, RunResult):
			# increment counter
			regMap['all'][0] += 1
			regMap[run.address][0] += 1
			# was there also an error?
			if run.result.errors:
				regMap['all'][1] += 1
				regMap[run.address][1] += 1
		else:
			# something else happened and the benchmark didn't finish
			regMap['all'][2] += 1
			regMap[run.address][2] += 1

	# print out
	print("Register: error / injections (other)")
	for reg, count in sorted(regMap.items()):
		print(" {:<4}: {:3} / {:3} ({})".format(
				reg, count[1], count[0], count[2])
		)


def examineErrorSpot(elfParser, run):
	"""Helper function for examineErrorAddresses.
	Looks at a single run to examine where the error occured.
	As in, what memory value was changed
	"""
	name = elfParser.findNearestSymbolName(run.address)
	return name

def examineErrorAddresses(d0, objPath=None):
	"""
	d0 - directory of log files
	objPath - path to objdump binary
	"""
	sum0, runs = parseOneDir(d0, keepRuns=True)
	# errDict = {}
	errSpots = []
	errOutOfRange = 0
	oorList = []

	# create the parser
	elfParser = elfUtils.ElfParser()
	# TODO: better checking for objdump path. How to inspect PATH?
	if objPath is not None:
		objDumpPath = objPath
	else:
		objDumpPath = "arm-none-eabi-objdump"
	elfParser.createSymTable(sum0.name, objDumpPath)

	for run in runs:
		if isinstance(run.result, RunResult):
			if run.result.errors > 0:
				errSpot = examineErrorSpot(elfParser, run)
				# if errSpot not in errDict:
				if (errSpot is None) and (run.address > int("0xFFFF0000", 16)):
					errOutOfRange += 1
					oorList.append(run)
					continue
				errSpots.append("{:08X}: {}".format(run.address, errSpot))

	for spot in sorted(errSpots):
		print(spot)
	if errOutOfRange > 0:
		print("{} Errors occurred injecting faults past 0xFFFF0000".format(errOutOfRange))
		for run in oorList:
			if run.cacheInfo is not None:
				if run.cacheInfo.dirty == False:
					print(" {}: {}".format(run.address, str(run.cacheInfo)))
	return


def examineSymbolInjections(d0, objPath=None):
	"""Examine which symbols were injected into, and if they caused errors
	d0 - directory of log files
	objPath - path to objdump binary
	"""
	sum0, runs = parseOneDir(d0, keepRuns=True)

	# create the parser
	elfParser = elfUtils.ElfParser()
	if objPath is not None:
		objDumpPath = objPath
	else:
		objDumpPath = "arm-none-eabi-objdump"
	elfParser.createSymTable(sum0.name, objDumpPath)

	symMap = {}
	strMap = {}
	noneCount = 0
	undefErrors = {}
	underErrorCount = 0
	totalErrs = sum0.errors

	print("Examining {}".format(d0))
	print("  Total errors: {}".format(sum0.errors))

	for run in runs:
		# what the name is
		sym = elfParser.findNearestSymbol(run.address)
		if sym is None:
			noneCount += 1
			if isinstance(run.result, (RunResult, AssertionFailResult)) \
					and run.result.errors > 0:
				if run.address not in undefErrors:
					undefErrors[run.address] = 0
				undefErrors[run.address] += 1
				underErrorCount += 1
			continue
		# string only
		if isinstance(sym, str):
			# shrink name
			if "+" in sym:
				symName = sym.split()[0]
			else:
				symName = sym
			# init in map if needed
			if symName not in strMap:
				strMap[symName] = {"count" : 0, "errors" : 0}
			# increment counters
			strMap[symName]["count"] += 1
			if isinstance(run.result, (RunResult, AssertionFailResult)) \
					and run.result.errors > 0:
				strMap[symName]["errors"] += 1
		# symbol object
		else:
			symName = sym.name
			# init in map if needed
			if sym.section not in symMap:
				symMap[sym.section] = {}
			if symName not in symMap[sym.section]:
				symMap[sym.section][symName] = {
					"count" : 0,
					"errors" : 0
				}
			# increment counters
			symMap[sym.section][symName]["count"] += 1
			if isinstance(run.result, (RunResult, AssertionFailResult)) \
					and run.result.errors > 0:
				symMap[sym.section][symName]["errors"] += 1

	# print out map
	for sectionName in symMap:
		sectMap = symMap[sectionName]
		print("\n === Section {} ===".format(sectionName))
		sectCount = 0
		sectErrors = 0

		for sym in sorted(sectMap, key=lambda name: sectMap[name]['errors'], reverse=True):
			info = sectMap[sym]
			outputStr = "{}, count: {}".format(sym, info['count'])
			sectCount += info['count']
			if info['errors'] > 0:
				outputStr += ", errors: {}".format(info['errors'])
				sectErrors += info['errors']
			print(outputStr)
		print("Total - count: {}, errors: {}".format(sectCount, sectErrors))
		if sectErrors > 0:
			print("\tError rate: {:02f}%".format((sectErrors / sectCount) * 100))
			totalErrs -= sectErrors

	# string-only symbols
	print("\n === Other ===")
	otherCount = 0
	otherErrors = 0
	for sym in sorted(strMap, key=lambda name: strMap[name]['errors'], reverse=True):
		info = strMap[sym]
		outputStr = "{}, count: {}".format(sym, info['count'])
		otherCount += info['count']
		if info['errors'] > 0:
			outputStr += ", errors: {}".format(info['errors'])
			otherErrors += info['errors']
		print(outputStr)
	print("Total - count: {}, errors: {}".format(otherCount, otherErrors))
	if otherErrors > 0:
		print("\tError rate: {:02f}%".format((otherErrors / otherCount) * 100))
		totalErrs -= otherErrors

	print("\nw/o symbol name: {}".format(noneCount))
	print("{} errors unaccounted for".format(totalErrs))
	if underErrorCount != totalErrs:
		print(" {}?\n".format(underErrorCount - totalErrs))
	else:
		print("")

	for addr in sorted(undefErrors, key=lambda addr: undefErrors[addr], reverse=True):
		print("  0x{:08X}: {}".format(addr, undefErrors[addr]))
	return


def compareRuns(sum0, sum1):
	# size comparison
	sz0 = os.path.getsize(sum0.name)
	sz1 = os.path.getsize(sum1.name)
	szChange = sz1 / sz0
	# prevent divide by 0 errors
	e0 = sum0.errors if sum0.errors > 0 else 1
	e1 = sum1.errors if sum1.errors > 0 else 1
	# calculate error rate
	rate0 = e0 / sum0.length
	rate1 = e1 / sum1.length
	errorChange = rate0 / rate1
	# run-time change
	rtChange = sum1.singleAvg / sum0.singleAvg
	# calculate MWTF - change in error rate / change in run-time
	mwtf = errorChange / rtChange
	# precision: default 3 unless part greater than 0 has more than 2 digits
	rtSig = len(str(int(errorChange)))
	rtPrec = 3 - (rtSig - 2)

	print("Comparing:")
	print("\t0: {} - {} total injections".format(sum0.name, sum0.length))
	if sum0.cacheFlag:
		print("\t\t({} not dirty)".format(sum0.cacheValids))
	print("\t1: {} - {} total injections".format(sum1.name, sum1.length))
	if sum1.cacheFlag:
		print("\t\t({} not dirty)".format(sum1.cacheValids))

	fstr = """
┏━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━┓
┃ Run     ┃ Faults   Errors   Timeout   Invalid ┃  Size    Runtime  ┃   error     MWTF    ┃
┃         ┃ (Fixed)   (SDC)    (Hang)    Status ┃  (KB)      (ms)   ┃   rate              ┃
┣━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━┫
┃  0      ┃ {f0:6}   {e0:6}   {t0:6}    {i0:6}  ┃ {s0:6}   {r0:6}   ┃  {rt0:>5.2f}%      -      ┃
┃  1      ┃ {f1:6}   {e1:6}   {t1:6}    {i1:6}  ┃ {s1:6}   {r1:6}   ┃  {rt1:>5.2f}%      -      ┃
┣━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━┫
┃ Change: ┃                                     ┃ {s:6.3f}x   {r:6.3f}x ┃ {rc:6.{rcp}f}x {m:9.3}x  ┃
┗━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━┛
"""

	output = fstr.format(
			f0=sum0.faults, e0=sum0.errors, t0=sum0.timeouts,
			i0=sum0.invalids, s0=int(sz0/1000), r0=int(sum0.singleAvg*1000),
			f1=sum1.faults, e1=sum1.errors, t1=sum1.timeouts,
			i1=sum1.invalids, s1=int(sz1/1000), r1=int(sum1.singleAvg*1000),
			rt0=rate0*100, rt1=rate1*100, rc=errorChange, rcp=rtPrec,
			s=szChange, r=rtChange, m=mwtf
	)
	print(output)

# returns the summary object
def parseOneFile(fname, pcstats=False):
	run, execName = readJsonFile(fname)
	sum0 = summarizeRuns(run, execName)
	summarizeTiming(run, sum0)
	otherStats(run, sum0)

	if pcstats:
		pcStats(run)

	return sum0


def parseOneDir(dname, keepRuns=False):
	flist = sorted(os.listdir(dname))
	runs = []
	times = []
	execName = None

	for f in flist:
		if ".json" in f:
			run, execName = readJsonFile(os.path.join(dname, f))
			runs.extend(run)
			tempSum = summarizeRuns(run, execName)
			summarizeTiming(run, tempSum)
			times.append(tempSum.totalTime)

	if not execName:
		print("No log files in directory {}".format(dname))
		sys.exit(-1)

	sum0 = summarizeRuns(runs, execName)
	# get timing data
	tDiffTotal = sum(times, timedelta())
	sum0.totalTime = tDiffTotal
	# time per injection
	sum0.singleTime = tDiffTotal.total_seconds() / len(runs)
	# summarizeTiming(runs, sum0)
	otherStats(runs, sum0)

	if keepRuns:
		return sum0, runs
	else:
		return sum0


def normalParse(fname, args):
	sum0 = parseOneFile(fname, pcstats=args.cycleStats)
	if not args.noSummary:
		print(sum0)

	if args.countTrap:
		print("{} of the timeouts were traps".format(sum0.traps))


def compareFiles(f0, f1):
	sum0 = parseOneFile(f0)
	sum1 = parseOneFile(f1)
	compareRuns(sum0, sum1)


def compareDirectories(d0, d1):
	sum0 = parseOneDir(d0)
	sum1 = parseOneDir(d1)
	compareRuns(sum0, sum1)


def main():
	args = parseCommandLine()

	if args.register_errors:
		countRegErrors(args.filename, args)
	elif args.examine_error_addresses:
		examineErrorAddresses(args.filename, args.examine_error_addresses)
	elif args.symbol_injection_count:
		examineSymbolInjections(args.filename, args.symbol_injection_count)
	elif args.compare_dirs:
		compareDirectories(args.filename, args.compare_dirs)
	elif args.compare_files:
		compareFiles(args.filename, args.compare_files)
	elif args.parse_dir:
		sum0 = parseOneDir(args.filename)
		print(sum0)
	else:
		normalParse(args.filename, args)


if __name__ == '__main__':
	main()
