.. COAST setup guide

Getting Started
*****************

What is LLVM?
================

For a good introduction to LLVM, please refer to http://www.cs.cornell.edu/~asampson/blog/llvm.html


Prerequisites
================

- Have a version of Linux that has ``cmake`` and ``make`` installed.

For reference, development of this tool has been done on Ubuntu 16.04.

Building LLVM
================

- Create a folder to house the repository.  It is recommended that the folder containing this repository be in your home directory.  For example, ``~/coast/``.

- Check out the project:

 ``git clone https://github.com/byuccl/coast.git ~/coast``

- Change to the "build" directory and configure the Makefiles.  Example invocation:

 ``cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_ASSERTIONS=On ../llvm/``

 See the ``README.md`` in the "build" folder for more information on how to further configure LLVM.

- Run ``make``.  This may take quite a while, up to an hour or more if you do not parallelize the job.  Adding the flag ``-jn`` allows you to parallelize across ``n`` cores.

.. note:: The higher the number the faster the builds will take, but the more RAM will be used. Parallelizing across 7 cores can take over 16 GB of RAM. If you run out of RAM, the compilation can fail. In this case simply re-run ``make`` without any parallelization flags to finish the compilation.

If you wish to add the LLVM binaries to your ``PATH`` variable, add the following to the end of your ``.bashrc`` file:

    ``export PATH="/home/$USER/coast/build/bin:$PATH"``

Building the Passes
=====================

To build the passes so they can be used to protect your code:

- Go the "projects" directory
- Run ``cmake ..``
- Run ``make``  (with optional ``-jn`` flag as before)
