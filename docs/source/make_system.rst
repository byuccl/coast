.. How to use the COAST Makefile system

Using the Makefile System
**************************

We have provided a set of Makefiles that can be used to build the benchmarks in the "tests" folder.  They are conditionally included to support building executables for various platforms without unnecessary code replication.

Targets
================

There are two Make targets commonly used by all of the Makefiles.  The first is ``exe``, which builds the executable itself.  The second is ``program``, which runs the executable.  If the target architecture is an external device, it will upload the file to the device.  If it is a local architecture, such as ``lli`` or ``x86``, then it will run on the host machine.  Some architectures incorporate FPGAs, and so have an additional Make target called ``configure``, which will upload a bitstream to the FPGA.

Extending the Makefile system
================================

Adding support for additional platforms requires a new Makefile be created that contains the build flow for the target platform.  The basic idea is to

1. Compile the source code to LLVM IR
2. Run the IR through ``opt`` (and enable the ``-DWC`` or ``-TMR`` passes as necessary)
3. Link the COAST protected code with any other object code in the project
4. Assemble to target machine language

Example
================

A good example to look at is the pseudo target ``lli``.  This is LLVM's target independent IR source interpreter.  It can execute ``.ll`` or ``.bc`` files (plain-text IR or compiled bytecode).  It is fairly simple because it does not require an assembly step.  For an example of converting from the protected IR to machine code, look at the Makefile for compiling to the Pynq architecture.
