###########################################################
# driver for running llvm-stress
###########################################################


import os
import time
import shlex
import pathlib
import argparse
import tempfile
import subprocess as sp

this_dir = pathlib.Path(__file__).resolve().parent
makefile_path = this_dir / "makefile.customFile"


def setUpArgs():
    parser = argparse.ArgumentParser(description="Run randomly generated .ll files (using llvm-stress) through DWC and TMR passes")
    parser.add_argument('passes', type=str, help='opt passes to run on generated IR')
    parser.add_argument('--tests', '-n', help='how many times to run the stress test (default 10)', type=int, default=10)
    parser.add_argument('--size', '-s', help='size will be passed to llvm-stress. indicates number of lines to generate', type=int, default=150)
    return parser.parse_args()


def createRandomIRFile(tempFile, size):
    # create file of IR
    path = tempFile.name
    cmd = "llvm-stress-7 -size={} -o {}".format(size, str(path))
    # print(cmd)
    proc = sp.Popen(shlex.split(cmd), stdout=sp.PIPE)
    output = proc.communicate()[0]
    # wait for file to be created
    while not os.path.exists(str(path)):
        time.sleep(0.5)
    # print errors
    if proc.returncode:
        print(output.decode())
    return proc.returncode

def runOpt(srcDir, targetPath, passes):
    # run file through optimizer and compile to assembly, but do not link, since there is no `main`
    cmd = "make --file={mk} 'PROJECT_SRC={dir}' 'TARGET={tgt}' 'OPT_PASSES={ps}' {tgt}.s"
    cmd = cmd.format(
        mk=makefile_path,
        dir=srcDir,
        tgt=targetPath,
        ps=passes)
    # print(cmd)
    proc = sp.Popen(shlex.split(cmd), cwd=srcDir, stdout=sp.PIPE)
    output = proc.communicate()[0]
    # print errors
    if proc.returncode:
        print(output.decode())
    return proc.returncode


def main():
    args = setUpArgs()
    llSuffix = ".lbc"

    with tempfile.TemporaryDirectory(dir=str(this_dir)) as td:
        for _ in range(args.tests):
            # create random .ll file
            with tempfile.NamedTemporaryFile(mode='w', dir=str(td), suffix=llSuffix) as llFile:
                rc0 = createRandomIRFile(llFile, args.size)
                if rc0:
                    print("Error creating IR file of size {}".format(args.size))
                    return rc0
                # run through optimizer (can directly take .ll files)
                rawFileName = llFile.name.replace(llSuffix, "")
                rawFileName = os.path.basename(rawFileName)
                rc1 = runOpt(str(td), rawFileName, args.passes)
                # don't need to remove opt file, because temp directory will be deleted
                if rc1:
                    print("Error running configuration {}".format(args.passes))
    
    # if success
    print("Success!")


if __name__ == '__main__':
    main()
