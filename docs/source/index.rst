.. COAST documentation master file, created by
   sphinx-quickstart on Mon Jul  8 13:39:59 2019.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

COAST
********

**CO**\ mpiler-\ **A**\ ssisted **S**\ oftware fault **T**\ olerance

.. toctree::
    :maxdepth: 2
    :caption: Contents:

    setup
    make_system
    passes
    repl_scope
    troubleshooting
    release_notes
    eclipse
    cfcss
    tests
    fault_injection


Folder guide
==============

boards
---------

This folder has support files needed for the various target architectures we have used in testing COAST.

build
---------

This folder contains instructions on how to build LLVM, and when built will contain the binaries needed to compile source code.  Note: building LLVM from source is optional.

projects
---------

The passes that we have developed as part of COAST.

rtos
---------

Example applications for FreeRTOS and how to use it with COAST.

simulation
-----------

Files for running fault injection campaigns.

tests
---------

Benchmarks we use to validate the correct operation of COAST.



Results
========

See the results of fault injection and radiation beam testing

.. toctree::
    :maxdepth: 1
    :glob:

    results/*

.. Add more results?


Additional Resources
=====================

- Matthew Bohman's `Master's thesis`_.

- IEEE Transactions on Nuclear Science, Vol. 66 Issue 1 - `Microcontroller Compiler-Assisted Software Fault Tolerance`_

- IEEE Transactions on Nuclear Science, Vol. 67 Issue 1 - `Applying Compiler-Automated Software Fault Tolerance to Multiple Processor Platforms`_

.. _Master's thesis: https://scholarsarchive.byu.edu/cgi/viewcontent.cgi?article=7724&context=etd

.. _Microcontroller Compiler-Assisted Software Fault Tolerance: https://ieeexplore.ieee.org/document/8571250

.. _Applying Compiler-Automated Software Fault Tolerance to Multiple Processor Platforms: https://ieeexplore.ieee.org/document/8933038
