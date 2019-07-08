.. This document explains the LLVM passes used to implement COAST

Passes
********

COAST consists of a series of LLVM passes. The source code for these passes is found in the "projects" folder. This section covers the different passes available and their functions.

Description
=============

- **CFCSS**\ : This implements a form of Control Flow Checking via Software Signatures [#f1]_\ . Basic blocks are assigned static signatures in compilation. When the code is executing it compares the current signature to the known static signature. This allows it to detect errors in the control flow of the program.
- **dataflowProtection**\ : This is the underlying pass behind the DWC and TMR passes.
- **debugStatements**\ : On occasion programs will compile properly, but the passes will introduce runtime errors. Use this pass to insert print statements into every basic block in the program. When the program is then run, it is easy to find the point in the LLVM IR where things went awry. Note that this incurs a **very** large penalty in both code size and runtime.
- **DWC**\ : This pass implements duplication with compare (DWC) as a form of data flow protection. DWC is also known as dual modular redundancy (DMR). It is based on EDDI [#f2]_. Behind the scenes, this pass simply calls the dataflowProtection pass with the proper arguments.
- **exitMarker**\ : For software fault injection we found it helpful to have known breakpoints at the different places that ``main()`` can return. This pass places a function call to a dummy function, ``EXIT MARKER``, immediately before these return statements. Breakpoints placed at this function allow debuggers to access the final processor state.
- **TMR**\ : This pass implements triple modular redundancy (TMR) as a form of data flow protection. It is based on SWIFT-R [#f3]_ and Trikaya [#f4]_. Behind the scenes, this pass simply calls the dataflowProtection pass with the proper arguments.

Configuration Options
======================

Command Line Parameters
-------------------------

These options are only applicable to the ``-DWC`` and ``-TMR`` passes.

.. table::
    :widths: 25, 40

    +---------------------------+-----------------------------------------------------+
    | Command line option       | Effect                                              |
    +===========================+=====================================================+
    |   ``-noMemReplication``   | Don’t replicate variables in memory (ie. use        |
    |                           | rule D2 instead of D1).                             |
    +---------------------------+-----------------------------------------------------+
    |      ``-noLoadSync``      | Don’t synchronize on data loads (C3).               |
    +---------------------------+-----------------------------------------------------+
    |    ``-noStoreDataSync``   | Don’t synchronize the data on data stores (C4).     |
    +---------------------------+-----------------------------------------------------+
    |    ``-noStoreAddrSync``   | Don’t synchronize the address on data stores (C5).  |
    +---------------------------+-----------------------------------------------------+

.. table::
    :widths: 25, 40

    +---------------------------+-----------------------------------------------------+
    |     ``-ignoreFns=<X>``    | <X> is a comma separated list of the functions      |
    |                           | that should not be replicated.                      |
    +---------------------------+-----------------------------------------------------+
    |    ``-ignoreGlbls=<X>``   | <X> is a comma separated list of the global         |
    |                           | variables that should not be replicated.            |
    +---------------------------+-----------------------------------------------------+
    |   ``-skipLibCalls=<X>``   | <X> is a comma separated list of library functions  |
    |                           | that should only be called once.                    |
    +---------------------------+-----------------------------------------------------+
    | ``-replicateFnCalls=<X>`` | <X> is a comma separated list of user functions     |
    |                           | where the body of the function should not be        |
    |                           | modified, but the call should be replicated         |
    |                           | instead.                                            |
    +---------------------------+-----------------------------------------------------+
    |    ``-configFile=<X>``    | <X> is the path to the configuration file that      |
    |                           | has these options saved.                            |
    +---------------------------+-----------------------------------------------------+

.. table::
    :widths: 25, 40

    +---------------------------+-----------------------------------------------------+
    |      ``-countErrors``     | Enable TMR to track the number of errors corrected. |
    +---------------------------+-----------------------------------------------------+
    | ``-runtimeInitGlbls=<X>`` | <X> is a comma separated list of the replicated     |
    |                           | global variables that should be initialized at      |
    |                           | runtime using memcpy.                               |
    +---------------------------+-----------------------------------------------------+
    |        ``-i or -s``       | Interleave (-i) the instruction replicas with the   |
    |                           | original instructions or group them together and    |
    |                           | place them immediately before the synchronization   |
    |                           | logic (-s). COAST defaults to -s.                   |
    +---------------------------+-----------------------------------------------------+
    |      ``-dumpModule``      | At the end of execution dump out the contents of    |
    |                           | the module to the command line. Mainly helpful      |
    |                           | for debugging purposes.                             |
    +---------------------------+-----------------------------------------------------+
    |        ``-verbose``       | Print out more information about what the pass      |
    |                           | is modifying.                                       |
    +---------------------------+-----------------------------------------------------+

Note: Replication rules defined by Chielle et al. [#f5]_\ .

In-code Directives
-------------------

.. table::
    :widths: 25, 40

    +----------------------+-------------------------------------------------------+
    |       Directive      | Effect                                                |
    +======================+=======================================================+
    |   ``__DEFAULT_xMR``  | Include at the top of the code. Set the default       |
    |                      | processing to be to replicate every piece of code     |
    |                      | except those specifically tagged. This is             |
    |                      | the default behavior.                                 |
    +----------------------+-------------------------------------------------------+
    | ``__DEFAULT_NO_xMR`` | Set the default behavior of COAST to not replicate    |
    |                      | anything except what is specifically tagged.          |
    +----------------------+-------------------------------------------------------+

.. table::
    :widths: 25, 40

    +----------------------+-------------------------------------------------------+
    |     ``__NO_xMR``     | Used to tag functions and variables that should       |
    |                      | not be replicated. Functions tagged in this manner    |
    |                      | behave as if they were passed to -ignoreFns.          |
    +----------------------+-------------------------------------------------------+
    |       ``__xMR``      | Designate functions and variables that should be      |
    |                      | cloned. This replicates function bodies and modifies  |
    |                      | the function signature.                               |
    +----------------------+-------------------------------------------------------+
    |   ``__xMR_FN_CALL``  | Available for functions only. The same as             |
    |                      | -replicateFnCalls above. Repeat function calls        |
    |                      | instead of modifying the function body.               |
    +----------------------+-------------------------------------------------------+


See the file COAST.h_

.. _COAST.h: https://github.com/byuccl/coast/blob/master/projects/dataflowProtection/COAST.h


Configuration File
--------------------

Instead of repeating the same command line options across several compilations, we have created a configuration file, "functions.config" that can capture the same behavior. It is found in the "dataflowProtection" pass folder. The location of this file can be specified using the ``-configFile=<...>`` option. The options are the same as the command line alternatives.


Details
--------

**Replication Rules**\ :

VAR3+, the set of replication rules introduced by Chielle et al. [#f5]_\ , instructs that all registers and instructions, except store instructions, should be duplicated. The data used in branches, the addresses before stores and jumps, and the data used in stores are all synchronized and checked against their duplicates. VAR3+ claims to catch 95% of data errors, so we used it as a starting point for automated mitigation. However, we removed rule D2, which does not replicate store instructions, in favor of D1, which does. This results in replication of all variables in memory, and is desirable as microcontrollers have no guarantee of protected memory. The synchronization rules are included in both DWC and TMR protection. Rules C1 and C2, synchronizing before each read and write on the register, respectively, are not included in our pass because these were shown to provide an excessive amount of synchronization. G1, replicating all registers, and C6, synchronizing before branch or store instructions, cannot be disabled as these are necessary for the protection to function properly.

The first option, ``-noMemReplication``, should be used whenever memory has a separate form of protection, such as error correcting codes (ECC). The option specifies that neither store instructions nor variables should be replicated. This can dramatically speed up the program because there are fewer memory accesses. Loads are still executed repeatedly from the same address to ensure no corruption occurs while processing the data.

The option ``-noStoreAddrSync`` corresponds to C5. In EDDI, memory was simply duplicated and each duplicate was offset from the original value by a constant. However, COAST runs before the linker, and thus has no notion of an address space. We implement rules C3 and C5, checking addresses before stores and loads, for data structures such as arrays and structs that have an offset from a base address. These offsets, instead of the base addresses, are compared in the synchronization logic.

**Replication Scope**\ :

The user can specify any functions and global variables that should not be protected using ``-ignoreFns`` and ``-ignoreGlbls``. At minimum, these options should be used to exclude code that interacts with hard- ware devices (GPIO, UART) from the SoR. Replicating this code is likely to lead to errors. The option ``-replicateFnCalls`` causes user functions to be called in a coarse grained way, meaning the call is replicated instead of fine-grained instruction replication within the function body. Library function calls can also be excluded from replication via the flag ``-skipLibCalls``, which causes those calls to only be executed once. These two options should be used when multiple independent copies of a return value should be generated, instead of a single return value propagating through all replicated instructions. Changing the scope of replication can cause problems across function calls.

**Other Options**\ :

*Error Logging*\ : This option was developed for tests in a radiation beam, where upsets are stochastically distributed, unlike fault injection tests where one upset is guaranteed for each run. COAST can be instructed to keep track of the number of corrected faults via the flag ``-countErrors``. This flag allows the program to detect corrected upsets, which yields more precise results on the number of radiation-induced SEUs. This option is only applicable to TMR because DWC halts on the first error. A global variable, ``TMR_ERROR_CNT``, is incremented each time that all three copies of the datum do not agree. If this global is not present in the source code then the pass creates it. The user can print this value at the end of program execution, or read it using a debugging tool.

*Error Handlers*\ : The user has the choice of how to handle DWC and CFCSS errors because these are uncorrectable. The default behavior is to create ``abort()`` function calls if errors are detected. However, user functions can be called in place of ``abort()``. In order to do so, the source code needs a definition for the function ``void FAULT_DETECTED_DWC()`` or ``void FAULT_DETECTED_CFCSS`` for DWC and CFCSS, respectively.

*Input Initialization*\ : Global variables with initial values provide an interesting problem for testing. By default, these initial values are assigned to each replicate at compile time. This models the scenario where the SoR expands into the source of the data. However, this does not accurately model the case when code inputs need to be replicated at runtime. This could happen, for instance, if a UART was feeding data into a program and storing the result in a global variable. When global variables are listed using ``-runtimeInitGlbls`` the pass inserts ``memcpy()`` calls to copy global variable data into the replicates at runtime. This supports scalar values as well as aggregate data types, such as arrays and structures.

*Interleaving*\ : In previous work replicated instructions have all been placed immediately after the original instructions. Interleaving instructions in this manner effectively reduces the number of available registers because each load statement executes repeatedly, causing each original value to occupy more registers. For TMR, this means that a single load instruction in the initial code uses three registers in the protected program. As a result, the processor may start using the stack as extra storage. This introduces additional memory accesses, increasing both the code size and execution time. Placing each set of replicated instructions immediately before the next synchronization point lessens the pressure on the register file by eliminating the need for multiple copies of data to be live simultaneously.

By default, COAST groups copies of instructions before synchronization points, effectively partitioning regions of code into segments where each copy of the program runs uninterrupted. Alternately, the user can specify that instructions should be interleaved using ``-i``.

*Printing Status Messages*\ : Using the ``-verbose`` flag will print more information about what the pass is doing. This includes removing unused functions and unused global strings. These are mainly helpful for examining when your code is not behaving exactly as expected.

If you are developing passes, then on occasion you might need to include more printing statements. Using ``-dumpModule`` causes the pass to print out the entirety of the LLVM module to the command line in a format that can be tested using ``lli``. This is mainly helpful if the pass is not cleaning up after itself properly. The function ``dumpModule()`` can also be placed in different places in the code for additional debugging capabilities.


.. rubric:: Footnotes

.. [#f1] N. Oh, P. P. Shirvani, and E. J. McCluskey, "Control-flow checking by software signatures," *IEEE Transactions on Reliability*\ , vol. 51, no. 1, pp. 111–122, Mar. 2002.
.. [#f2] ——, "Error detection by duplicated instructions in super-scalar processors," *IEEE Transactions on Reliability*\ , vol. 51, no. 1, pp. 63–75, Mar. 2002.
.. [#f3] J. Chang, G. Reis, and D. August, "Automatic Instruction-Level Software-Only Recovery," in *International Conference on Dependable Systems and Networks (DSN’06)*\ . IEEE, 2006, pp. 83–92.
.. [#f4] H. Quinn, Z. Baker, T. Fairbanks, J. L. Tripp, and G. Duran, "Software Resilience and the Effectiveness of Software Mitigation in Microcontrollers," in *IEEE Transactions on Nuclear Science*\ , vol. 62, no. 6, Dec. 2015, pp. 2532–2538.
.. [#f5] E. Chielle, F. L. Kastensmidt, and S. Cuenca-Asensi, "Overhead reduction in data-flow software-based fault tolerance techniques," in *FPGAs and Parallel Architectures for Aerospace Applications: Soft Errors and Fault-Tolerant Design*\ . Cham: Springer International Publishing, 2015, pp. 279–291.
