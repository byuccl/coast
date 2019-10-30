.. guide to using Eclipse with LLVM

Using Eclipse with LLVM
************************

You can write your code in a plain text editor, or you can use Eclipse to help you manage all of the classes and methods. This guide was written for Eclipse 4.10.0 using the CDT.

Setting up the project
=========================

1. Select "File -> New -> Makefile Project with Existing Code".
2. Enter ``projects`` as the project name.
3. For the existing code location field, browse to the projects directory
4. Use the "Linux GCC" toolchain.
5. Right click on your project directory and select "Properties"
6. Navigate to "C/C++ Build" and change the build directory to your ``projects/build`` folder using the "File system" button.
7. Change to the "Behavior" tab and enable parallel builds. We recommend using 3-4 parallel jobs.
8. Click "Apply" then "Apply and Close".
9. When you click on the "Build" button the projects will be compiled.

Building the projects
==========================

1. Right click on the ``projects/build`` subdirectory, then "Make Targets -> Create".
2. Call the target name ``all`` and click OK.
3. To build your pass, right click on the build folder and click "Make Targets -> Build -> Build" (with the target ``all`` selected).
4. After the first time that you’ve done this, you can rebuild all your passes by pressing ``F9``.


Fixing the CDT settings
========================

The default settings of the project are not sufficient to allow the Eclipse CDT indexer to work correctly.  While not necessary to fix the CDT settings, it allows you to use the auotcomplete functionality of Eclipse.

1. Right-click on the project and select "Properties"
#. Under "C/C++ General" select "Paths and Symbols"
#. Add a new Include Directory using the "Add" button
#. Select "File System"
#. Navigate to the repository root, then select ``llvm/include``
#. Check the box "Add to all languages," then click "OK"
#. On the left pane, select "Preprocessor Include Paths, Macros, etc"
#. On the "Providers" select "CDT GCC Built-in Compiler Settings"
#. Edit the "Command to get compiler specs" by putting ``std=c++11`` right before ``${INPUTS}``
#. Move the entry "CDT GCC Built-in Compiler Settings" to the top of the list using the "Move Up" button
#. Select "Apply and Close"

1. Select "Window" -> "Preferences"
#. Select "C/C++" -> "Build" -> "Settings"
#. Under the "Discovery" tab select "CDT GCC Built-in Compiler Settings"
#. Edit the "Command to get compiler specs" the same as before
#. Select "Apply and Close"
