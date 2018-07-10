#ifndef __TROPIC_MACROS__
#define __TROPIC_MACROS__

//This file contains the macros for the TROPIC pass.
//The annotations need to match those in dataflowProtection.h

//Macros for variables, functions
#define __NO_xMR __attribute__((annotate("no_xMR")))
#define __xMR __attribute__((annotate("xMR")))

//Macro for function calls - same as replicateFnCalls
#define __xMR_FN_CALL __attribute__((annotate("xMR_call")))

//Macros to set the default behavior of the code 
#define __DEFAULT_xMR int __attribute__((annotate("set_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;
#define __DEFAULT_NO_xMR int __attribute__((annotate("set_no_xMR_default"))) __xMR_DEFAULT_BEHAVIOR__;

#endif
