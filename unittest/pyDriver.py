###########################################################
# top level driver for running specialized Python driver
#  code in other directories
###########################################################

import re
import sys
import yaml
import shlex
import pathlib
import argparse
import subprocess as sp

from unittest import COAST_dir, error, bcolors


class Driver:
    def __init__(self, path):
        self.path = path
        self.relpath = self.path.relative_to(COAST_dir)
        self.target = None
        self.re = None

    def run(self, passes):
        # the command includes the current opt args
        runPath = self.path if sys.version_info.minor > 5 else str(self.path)
        runDir = str(self.path.parents[0])
        cmd = "python3 {} ".format(runPath)
        cmd += "\" {}\"".format(" ".join(passes.split()))
        # print(cmd)
        # these drivers should be self-cleaning
        s = sp.Popen(shlex.split(cmd), cwd=runDir, stdout=sp.PIPE, stderr=sp.STDOUT)
        stdout = s.communicate()[0].decode()
        # check the return value
        if s.returncode:
            print(stdout)
            error("Could not run", runPath)

        if self.re is not None:
            m = re.search(self.re, stdout)
            if not m:
                print(stdout)
                error("Could not match stdout of", runPath,
                    "using re expression:", self.re)


def main():
    # Load command-line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('config_yml')
    args = parser.parse_args()

    # Ensure yaml config file exists, then open and read it
    cfg_path = pathlib.Path(args.config_yml)
    if not cfg_path.is_file():
        error("Config file", cfg_path, "does not exist.")
    # pathlib doesn't work with open in 3.5-
    if sys.version_info.minor <= 5:
        cfg_path = str(cfg_path)
    with open(cfg_path, 'r') as stream:
        try:
            cfg = yaml.safe_load(stream)
        except yaml.YAMLError as exc:
            print(exc)

    # get the correct driver paths
    drivers = []
    for d in cfg["drivers"]:
        d_path = COAST_dir / d["path"]
        if not d_path.is_file():
            error("Driver file", d_path, "does not exist.")
        tmp_driver = Driver(d_path)
        # get the regex to match output
        if "re" in d:
            tmp_driver.re = d["re"]
        drivers.append(tmp_driver)

    # run over all the configuration options, running each driver
    for opt_pass in cfg["OPT_PASSES"]:
        print(bcolors.HEADER + "OPT_PASSES:", opt_pass, bcolors.ENDC)
        for driver in drivers:
            print("  " + bcolors.OKBLUE + str(driver.relpath), bcolors.ENDC)
            print("    Running")
            driver.run(opt_pass)


if __name__ == "__main__":
    main()
