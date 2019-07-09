Build LLVM by using CMAKE to create the Makefile:

```
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_ASSERTIONS=On ../llvm/
```

To build with the RISC-V backend enabled, add the flag `-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=RISCV`

Then run make.  Make sure to do a parallel build, otherwise it will take a long time:

```
make -j4
```
