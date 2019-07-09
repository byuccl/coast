cmake_minimum_required(VERSION 3.5)

set ( LLVM_ROOT ..)
if(NOT EXISTS ${LLVM_ROOT})
	message ( FATAL_ERROR " LLVM_ROOT (${LLVM_ROOT}) is invalid ")
endif ()

set(LLVM_DIR ../build/cmake/modules/CMakeFiles)

find_package ( LLVM REQUIRED CONFIG NO_DEFAULT_PATH)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(HandleLLVMOptions)
include(AddLLVM)


list (APPEND LLVM_INCLUDE_DIRS "${LLVM_ROOT}/llvm/include")

add_definitions ( ${LLVM_DEFINITIONS})
include_directories ( ${LLVM_INCLUDE_DIRS})

add_subdirectory (debugStatements)
add_subdirectory (exitMarker)
add_subdirectory (errorBlocks)
add_subdirectory (CFCSS)
add_subdirectory (dataflowProtection)
add_subdirectory (DWC)
add_subdirectory (TMR)
