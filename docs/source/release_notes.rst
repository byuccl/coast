.. This document explains the changes in the releases

Release Notes
**************


v1.5 - October 2020
=====================

Fault Injection Supervisor
---------------------------

Python scripts which comprise the :ref:`fault_injection` interface.


FreeRTOS Example Applications
------------------------------

Example :ref:`freertos_apps` that run on the FreeRTOS kernel, plus how to protect them with COAST.

Documentation also updated to include information about the :ref:`baremetal_tests`.


v1.4 - August 2020
=====================

Features
---------

- Support for cloning function return values

- New unit tests

- Better copying of debug info

- Experimental stack protection

- | 7 new command line arguments
  | See :ref:`coast_cli_params` for more information.


Directives
------------

7 new directives

- ``__ISR_FUNC``
- ``__xMR_RET_VAL``
- ``__xMR_PROT_LIB``
- ``__xMR_ALL_AFTER_CALL``
- ``__xMR_AFTER_CALL``
- ``__NO_xMR_ARG``
- ``__COAST_NO_INLINE``

See :ref:`in_code_directives` for more information.


Bug Fixes
-------------

- Correct support for variadic functions
- Fix up debug info for global variables so it works better with GDB
- Better removal of unused functions
- Official way of marking ISR functions instead of function name text matching



v1.3 - November 2019
=====================

Changed the source of the LLVM project files from SVN (`deprecated <https://llvm.org/docs/Proposals/GitHubMove.html>`_) to the Git mono-repo, `version 7.1.0 <https://github.com/llvm/llvm-project/tree/llvmorg-7.1.0>`_.


v1.2 - October 2019
====================


Features
---------

- Support for ``invoke`` instructions.

- Replication rules, does NOT sync on stores by default, added flag to enable turning that on (``-storeDataSync``).

- Support for compiling multiple files in the same project at different times (using the ``-noMain`` flag).

- Before running the pass, validates that the replication rules given to COAST are consistent with themselves.

- Can sync on vector types.

- Added more unit tests, along with a test driver.


Directives
------------

- Added directive ``__SKIP_FN_CALL`` that has the same behavior as ``-skipFnCalls=`` command line parameter.

- Can add option to not check globals crossing Sphere of Replication (``__COAST_IGNORE_GLOBAL(name)``).

- Added directive macro for marking variables as volatile.

- Treats any globals or functions marked with ``__attribute__((used))`` as volatile and will not remove them.  Also true for globals used in functions marked as "used".

- Added wrapper macros for calling a function with the clones of the arguments.  Useful for ``printf()`` and ``malloc()``, etc, when you only want specific calls to be replicated.


Bug Fixes
-------------

Thanks to Christos Gentsos for pointing out some errors in the code base.

- Allow more usage of function pointers by printing warning message instead of crashing. 

- Added various missing ``nullptr`` checks.

- Fixed crashing on some ``void`` return type functions.

- Better cleanup of stale pointers.


Debugging Tools
-----------------

- Added an option to the ``DebugStatements`` pass that only adds print statements to specified functions.

- Created a simplistic profiling pass called ``SmallProfile`` that can collect function call counts.

- Support for preserving debug info when source is compiled with debug flags.
