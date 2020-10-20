.. Troubleshooting

Troubleshooting
*******************

Although it is unlikely, there is a possibility that COAST could cause user code to crash. This is most often due to complications over what should be replicated, as described in the :ref:`when_repl_cmds` and :ref:`repl_scope` sections. If the crash occurs during compilation, please submit a report to jgoeders@byu.edu or `create an issue`_. If the code compiles but does not run properly, here are several steps we have found helpful. Note that running with DWC often exposes these errors, but TMR silently masks incorrect execution, which can make debugging difficult.

.. _create an issue: https://github.com/byuccl/coast/issues

Troubleshooting Ideas
=======================

- Check to see if the program runs using ``lli`` before and after the optimizer, then test if the generated binary runs on your platform. This allows you to test that ``llc`` is operating properly.
- You cannot replicate functions that are passed by reference into library calls. This may or may not be possible in user calls. Use ``-ignoreFns`` for these.
- For systems with limited resources, duplicating or triplicating code can take up too much RAM or ROM and cause the processor to halt. Test if a smaller program can run.
- The majority of bugs that we have encountered have stemmed from incorrect usage of customization. Please refer to :ref:`when_repl_cmds` and ensure that each function call behaves properly. Many of these bugs have stemmed from user wrappers to ``malloc()`` and ``free()``. The call was not replicated, so all of the instructions operated on a single piece of data, which caused multiple ``free()`` calls on the same memory address.
- Another point of customization to be aware of is how to handle hardware interactions. Calls to hardware resources, such as a UART, should be marked so they are not replicated unless specifically required.
- Be aware of synchronization logic. If a variable changes between accesses of instruction copies, such as volatile hardware registers, then the copies will fail when compared.
- Use the ``-debugStatements`` flag to explore the IR and find the exact point of failure.  See the :ref:`dbg_tools` section for more information.
- You may get an error that looks something like ``undefined symbol: ZTV18dataflowProtection`` when you try to run DWC or TMR. This occurs when you do not load the dataflowProtection pass before the DWC or TMR pass. Include ``-load <Path to dataflow protection.so>`` in your call to ``opt``.
- If compiling a C++ project, be aware that the compiler will often `mangle <https://en.wikipedia.org/wiki/Name_mangling#C++>`_ the names of functions.  In this case, the function names passed in to COAST may need to be changed.  Examine the LLVM IR output being given to ``opt`` to make sure they are correct.
