#ifndef __COAST_MACROS__
#define __COAST_MACROS__

/*
 * This file contains the macros for the COAST pass.
 * The annotations need to match those in dataflowProtection.h
 * See documentation for how to use these properly.
 */

// Macros for variables, functions
#define __NO_xMR __attribute__((annotate("no_xMR")))
#define __xMR __attribute__((annotate("xMR")))

// Macro for function calls - same as replicateFnCalls
#define __xMR_FN_CALL __attribute__((annotate("xMR_call")))
// same as skipLibCalls
#define __SKIP_FN_CALL __attribute__((annotate("coast_call_once")))

// Macros to set the default behavior of the code
#define __DEFAULT_xMR int __attribute__((annotate("set_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;
#define __DEFAULT_NO_xMR int __attribute__((annotate("set_no_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;

// The variable should not be optimized away
// Formerly a separate annotation, now use GCC "used" annotation
#define __COAST_VOLATILE __attribute__((used))

// This function is an Interrupt Service Routine (ISR)
#define __ISR_FUNC __attribute__((annotate("isr_function")))

// Replicate the return values of this function
#define __xMR_RET_VAL __attribute((annotate("repl_return_val")))

// This function will be a protected library function (don't change signature)
#define __xMR_PROT_LIB __attribute((annotate("protected_lib")))

// Clone function arguments *after* the call (ie. for scanf)
// There is a version which clones all of the args for every function call
#define __xMR_ALL_AFTER_CALL __attribute((annotate("clone-after-call-")))
// And another version which can specificy argument numbers for each call
// Specifiy the arg numbers as (name, 1_2_3)
// Linters might not like the underscores, but it's needed for valid function names
// They must also be registered, similar to below, to make it through the compiler
#define __xMR_AFTER_CALL(fname, x) fname##_CLONE_AFTER_CALL_##x

// Register a function as one which wraps malloc()
#define MALLOC_WRAPPER_REGISTER(fname) void* fname##_COAST_WRAPPER(size_t size)
#define MALLOC_WRAPPER_CALL(fname, x) fname##_COAST_WRAPPER((x))

// Also one which wraps printf, or something like it
#define PRINTF_WRAPPER_REGISTER(fname) int fname##_COAST_WRAPPER(const char* format, ...)
#define PRINTF_WRAPPER_CALL(fname, fmt, ...) fname##_COAST_WRAPPER(fmt, __VA_ARGS__)

// A generic macro for any kind of wrapper you want to use
#define GENERIC_COAST_WRAPPER(fname) fname##_COAST_WRAPPER

// COAST normally checks that a replicated global is used only in
//  protected functions.  This is a directive that goes right before
//  a function, with the name of the global to ignore boundary crossing
#define __COAST_IGNORE_GLOBAL(name) __attribute__((annotate("no-verify-"#name)))

// This directive is used to tell COAST that the argument [num] should not be replicated.
// If multiple arguments need to be marked this way, this directive should be placed
//  on the function multiple times.
#define __NO_xMR_ARG(num) __attribute__((annotate("no_xMR_arg-"#num)))

// convenience for no-inlining functions
#define __COAST_NO_INLINE __attribute__((noinline))

#endif
