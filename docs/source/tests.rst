.. This document explains the tests, both baremetal and rtos

Tests
******


.. _baremetal_tests:

Baremetal Benchmarks
=====================

In the course of developing COAST, it became necessary to validate that COAST-protected code operates as expected.  We have collected a number of benchmarks to put COAST through different use cases.  Some of these can be run on Linux, and others have been built to target a specific architecture.

Some of the tests are from known test suites, adapted to work with COAST.  Others are of our own concoction.  The tests are found in the repo `in this directory <https://github.com/byuccl/coast/tree/master/tests>`_  We list some noteworthy directories below:

- `aes <https://github.com/byuccl/coast/tree/master/tests/aes>`_ - An implementation of AES, borrowed from `this repo <https://github.com/lanl/benchmark_codes>`_ along with ``cache_test``, ``matrixMultiply``, and ``qsort``.

- `chstone <https://github.com/byuccl/coast/tree/master/tests/chstone>`_ - adapted from `CHStone test suite <http://www.ertl.jp/chstone/>`_.

- `makefiles <https://github.com/byuccl/coast/tree/master/tests/makefiles>`_ - the backbone of the testing setup, this directory has all of the files for configuring `GNU Make <https://www.gnu.org/software/make/>`_ to run the tests.

- `TMRregression/unitTests <https://github.com/byuccl/coast/tree/master/tests/TMRregression/unitTests>`_ - Small unit tests which test very specific COAST functionality.  Corner cases usually uncovered when trying to protect larger applications.  The directory ``TMRregression`` contains scripts for running these and other tests.


.. _freertos_apps:

FreeRTOS Applications
======================

Protecting a FreeRTOS kernel and application is much more complex a task than protecting a baremetal program.  The files can be found `here <https://github.com/byuccl/coast/tree/master/rtos/pynq>`_, and the COAST configuration needed to get the applications to work is detailed in the `Makefile <https://github.com/byuccl/coast/tree/master/rtos/pynq/Makefile>`_.
