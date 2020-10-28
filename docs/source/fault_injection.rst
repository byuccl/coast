.. Details about fault injection

.. _fault_injection:

Fault Injection
****************

To supplement the testing done in actual high-radiation environments, we have developed a system to inject faults into the applications we want to test.  This system is built on `QEMU <https://www.qemu.org/>`_, the Quick EMUlator.  We currently support the ARM Cortex-A9 processor, the main processing unit found in the Zynq-7000 SoC, a part we have often used in radiation tests.

The basic idea is to have a QEMU instance running the application that also runs a GDB stub.  Using the GDB interface, we can change values in the memory or registers as desired.  We utilize a QEMU plugin to keep track of exactly how cycles have elapsed so that the faults injected can be distributed evenly through time.

Instructions for building QEMU and the associated plugins can be found in the `README <https://github.com/byuccl/qemu/blob/cache-sim/README.rst#building>`_ of our QEMU fork.

Instructions for using the fault injector can be found by executing 

.. code-block:: bash

    python3 supervisor.py -h

in the directory ``coast/simulation/platform``.
