.. This file describes what the Scope of Replication is and why its integrity must be maintained

.. _scope_of_replication:

Scope of Replication
**********************

We use the term Sphere of Replication (SoR) to indicate which portions of the source code are to be protected.  In large applications, it may be too much overhead to have the entire program protected by COAST, so there is a way to configure COAST to only protect certain functions, using macros found in the header file `COAST.h <https://github.com/byuccl/coast/blob/master/tests/COAST.h>`_.


Configuration
===============

COAST allows for very detailed control over what belongs inside or outside of the Scope of Replication.  There are numerous :ref:`coast_cli_params` and :ref:`in_code_directives` which allow for projects to be configured very precisely.  COAST even includes a verification step that tries to ensure all SoR rules are self-consistent.  It can detect if protected global variables are used inside unprotected functions, or vice-versa.  However, this system is not perfect, and so the application writer must be aware of the potential pitfalls that could be encountered when using specific replication rules.


Pointer Crossings
==================

One of the most common problems to be aware of is pointers which cross the SoR boundaries.  Many applications use dynamically allocated memory.  If the function that allocates this memory is inside the SoR, then *all* references to these addresses must also be within the SoR.  It is true that read-only access would not cause errors, as in the case of using ``printf`` to view the value of such a pointer.  But no writes can happen outside the SoR, otherwise the addresses will get out of sync.


Example
==========

The unit test `linkedList.c <https://github.com/byuccl/coast/blob/master/tests/TMRregression/unitTests/linkedList.c>`_ shows exactly how SoR crossings can go wrong by looking at a possible implementation of a linked list.
