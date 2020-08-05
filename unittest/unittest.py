import sys
import argparse
import pathlib
import yaml
import subprocess
import re

COAST_dir = pathlib.Path(__file__).resolve().parent.parent
tests_dir = COAST_dir / "tests"

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class NoDesignInPath(Exception):
    pass

def error(*msg, return_code=-1):
    print("!!!! ERROR:", " ".join(str(item) for item in msg))
    sys.exit(return_code)

class Benchmark:
    def __init__(self, path):
        self.path = path
        self.relpath = self.path.relative_to(tests_dir)
        self.target = None
        self.re = None

        # Check if directory contains a valid design.
        # The current methed checks if it is a design by seeing if it contains
        # a Makefile with a 'TARGET = ' line.

        makefile_path = self.path / 'Makefile'
        if not makefile_path.is_file():
            raise NoDesignInPath

        if makefile_path.is_file():
            makefile_text = open(str(makefile_path), 'r').read()
            m = re.search(r"^\s*TARGET\s*(:|\+)?=\s*(.*?)\s*$", makefile_text, re.M)
            if m:
                self.target = m.group(2)
            else:
                raise NoDesignInPath

    # Compile the benchmark for x86 using provided opt_passes
    def compile(self, opt_passes):
        # Clean design dir
        cmd = ["make", "clean"]
        s = subprocess.run(cmd, cwd=str(self.path), stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL)
        if s.returncode:
            error("Could not clean", self.path)

        # Compile design
        cmd = ["make", "exe", "BOARD=x86", "OPT_PASSES=" + opt_passes]
        s = subprocess.Popen(
            cmd, cwd=str(self.path), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        stdout = s.communicate()[0].decode()
        if s.returncode:
            print(stdout)
            error("Could not compile", self.path)

    # Run the x86 compiled benchmark (must call compile first)
    def run(self):
        design_exe_path = str(self.path / (self.target + ".out"))
        cmd = [design_exe_path, ]
        s = subprocess.Popen(
            cmd, cwd=str(self.path), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        stdout = s.communicate()[0].decode()
        if s.returncode:
            print(stdout)
            error("Could not run", design_exe_path)
        
        # If there is a regex matcher, check the stdout against it
        if self.re is not None:
            m = re.search(self.re, stdout)
            if not m:
                print(stdout)
                error("Could not match stdout of", design_exe_path,
                    "using re expression:", self.re)

def find_all_benchmarks_in_path(path):
    benchmarks = []

    for d in path.glob('**/'):
        try:
            benchmark = Benchmark(d)
        except NoDesignInPath:
            pass
        else:
            benchmarks.append(benchmark)

    return benchmarks

def main():
    # Load command-line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('config_yml')
    args = parser.parse_args()

    # Ensure yaml config file exists, then open and read it
    cfg_path = pathlib.Path(args.config_yml)
    if not cfg_path.is_file():
        error("Config file", cfg_path, "does not exist.")
    with open(str(cfg_path), 'r') as stream:
        try:
            cfg = yaml.safe_load(stream)
        except yaml.YAMLError as exc:
            print(exc)

    # Find all benchmarks to run
    benchmarks = []
    for bench_cfg in cfg["benchmarks"]:
        bench_dir = tests_dir / bench_cfg["path"]
        benchmarks_on_path = find_all_benchmarks_in_path(bench_dir)
        if len(benchmarks_on_path) == 0:
            error("No benchmarks found at", bench_dir)
        if "re" in bench_cfg:
            for b in benchmarks_on_path:
                b.re = bench_cfg["re"]
        benchmarks.extend(benchmarks_on_path)

    # Loop through all OPT passes, and build/run each benchmark for this pass configuration
    for opt_pass in cfg["OPT_PASSES"]:
        print(bcolors.HEADER + "OPT_PASSES:", opt_pass, bcolors.ENDC)
        for benchmark in benchmarks:
            print("  " + bcolors.OKBLUE + str(benchmark.relpath), bcolors.ENDC)
            print("    Compiling")
            benchmark.compile(opt_pass)
            if benchmark.re is not None:
                print("    Running and validating output")
            else:
                print("    Running")
            benchmark.run()


if __name__ == "__main__":
    main()
