#ifndef __TROPIC_MACROS__
#define __TROPIC_MACROS__

//This file contains the macros for the TROPIC pass.
//The annotations need to match those in dataflowProtection.h

//Macros for variables, functions
#define __NO_xMR __attribute__((annotate("no_xMR")))
#define __xMR __attribute__((annotate("xMR")))

//Macro for function calls - same as replicateFnCalls
#define __xMR_FN_CALL __attribute__((annotate("xMR_call")))
//same as skipLibCalls
#define __SKIP_FN_CALL __attribute__((annotate("coast_call_once")))

//Macros to set the default behavior of the code
#define __DEFAULT_xMR int __attribute__((annotate("set_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;
#define __DEFAULT_NO_xMR int __attribute__((annotate("set_no_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;

//The variable should not be optimized away
#define __COAST_VOLATILE __attribute__((annotate("coast_volatile")))

//register a function as one which wraps malloc()
#define MALLOC_WRAPPER_REGISTER(fname) void* fname##_COAST_WRAPPER(size_t size);
#define MALLOC_WRAPPER_CALL(fname, x) fname##_COAST_WRAPPER((x))

// also one which wraps printf, or something like it
#define PRINTF_WRAPPER_REGISTER(fname) int fname##_COAST_WRAPPER(const char* format, ...);
#define PRINTF_WRAPPER_CALL(fname, fmt, ...) fname##_COAST_WRAPPER(fmt, __VA_ARGS__)

#define GENERIC_COAST_WRAPPER(fname) fname##_COAST_WRAPPER

// COAST normally checks that a replicated global is used only in
//  protected functions.  This is a directive that goes right before
//  a function, with the name of the global to ignore boundary crossing
#define __COAST_IGNORE_GLOBAL(name) __attribute__((annotate("no-verify-"#name)))

#endif
