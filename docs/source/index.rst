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

COAST has been validated multiple times using fault injection and radiation beam testing.  A number of these results have been published and are summarized below:

`Compiler-Assisted Software Fault Tolerance for Microcontrollers`_ - Matthew Bohman's Masters Thesis

- | Fault injection testing of MSP430 (Chapter 4)
  | Mean work to failure (MWTF) increased on average by 20x

- | Radiation testing of MSP430 (Chapter 5)
  | MWTF increased by 2.4x -- 3.9x


`Microcontroller Compiler-Assisted Software Fault Tolerance`_ - IEEE Transactions on Nuclear Science, Vol. 66 Issue 1

- | Fault injection testing of MSP430
  | MWTF increased by 11.4x -- 28.0x

- | Radiation testing of MSP430
  | MWTF increased by 4.3x -- 7.1x


`Applying Compiler-Automated Software Fault Tolerance to Multiple Processor Platforms`_ - IEEE Transactions on Nuclear Science, Vol. 67 Issue 1

- | Radiation testing of SiFive HiFive Freedom E310 (RISC-V)
  | MWTF increased by 2.0x -- 35.9x

- | Radiation testing of PYNQ-Z1 ARM Cortex-A9
  | MWTF increased by 1.2x -- 10.0x

- | Radiation testing of AVNET Ultra96 ARM Cortex-A53
  | Not enough errors observed during test


.. _Compiler-Assisted Software Fault Tolerance for Microcontrollers: https://scholarsarchive.byu.edu/cgi/viewcontent.cgi?article=7724&context=etd

.. _Microcontroller Compiler-Assisted Software Fault Tolerance: https://ieeexplore.ieee.org/document/8571250

.. _Applying Compiler-Automated Software Fault Tolerance to Multiple Processor Platforms: https://ieeexplore.ieee.org/document/8933038
