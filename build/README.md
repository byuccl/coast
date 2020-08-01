# Install LLVM

This project depends on LLVM and `clang` v7.0.0.

No support is offered for other versions.

## Option 1: System Packages

#### Ubuntu 18.04 and up:

```
sudo apt install llvm-7
sudo apt install clang-7
```

#### Other systems:

Check the package manager for the system you're on.

## Option 2: Official Releases

Check the [Official Releases page](https://github.com/llvm/llvm-project/releases) for downloads.

## Option 3: Build from Source

Build LLVM and `clang` by using CMAKE to create the Makefile.  In this directory, run:

```
cmake -G "Unix Makefiles" -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_ASSERTIONS=On ../llvm-project/llvm/
```

To build with the RISC-V backend enabled, add the flag `-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=RISCV`

Then run `make`.  Make sure to do a parallel build, otherwise it will take a long time:

```
make -j4
```

Not enabling debug or assertions will make the compile time faster. However, if you are developing passes, having debug enabled is well worth the wait.  In fact, developing passes is probably the only reason you'd want to build from source.  Otherwise, just use the package.
