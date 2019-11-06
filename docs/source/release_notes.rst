.. This document explains the changes in the releases

Release Notes
**************


November 2019
==============

Changed the source of the LLVM project files from SVN (`deprecated <https://llvm.org/docs/Proposals/GitHubMove.html>`_) to the Git mono-repo, `version 7.1.0 <https://github.com/llvm/llvm-project/tree/llvmorg-7.1.0>`_.


October 2019
==============


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
