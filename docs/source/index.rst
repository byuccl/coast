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

Folder guide
==============

build
---------

This folder contains instructions on how to build LLVM, and when built will contain the binaries needed to compile source code.

llvm
---------

The source code for LLVM and associated tools.

projects
---------

The passes that we have developed as part of COAST.

tests
---------

Benchmarks we use to validate the correct operation of COAST.
