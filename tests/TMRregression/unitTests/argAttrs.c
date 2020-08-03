/*
 * argAttrs.c
 * This unit test created to make sure COAST respects attributes
 * given to function arguments.
 * 
 * This guy says they must come after the argument type and name:
 * http://unixwiz.net/techtips/gnu-c-attributes.html
 * However, if we look at an example attribute like nonnull
 * https://gcc.gnu.org/onlinedocs/gcc-4.3.0/gcc/Function-Attributes.html#:~:text=nonnull%(arg
 * We see that the attribute actually requires the application programmer
 *  to specify the argument you want to be marked as such.
 * So our implementation will do something similar.
 * 
 * See the last section of processAnnotations() in interface.cpp
 * And the TODO on line 662 of cloning.cpp
 * 
 * The reason this unit test was created is when trying to protect only
 *  the kernel of a FreeRTOS application, both a TMR'd and normal version
 *  of the function xQueueReceive (and others) was being called.
 * This caused certain kernel globals to be used in- and out-side the scope
 *  of replication.
 * We un-protected the globals (like xTimerQueue), but COAST was still
 *  replicating the function args of xQueueReceive.  We wished to be able
 *  to mark the function args to not be replicated, but COAST ignored
 *  putting the macro `__NO_xMR` onto the argument.
 * Previously, this was only marking the "alloca" instruction to not xMR,
 *  but this would not affect the actual arguments being passed in.
 * The new implementation uses function attributes which specify arguments.
 *
 * This test is to make sure that what we changed fixed that error.
 * Make sure it also still removes the unused original function (if possible).
 */

#include <stdio.h>
#include <stdint.h>

#include "COAST.h"


/*
 * This function takes in a pointer argument that should not be replicated.
 */
// this way doesn't work:
// uint32_t passPointer(uint32_t* ptr __NO_xMR, uint32_t val)
// have to use the function annotation instead
__NO_xMR_ARG(0)
uint32_t passPointer(uint32_t* ptr, uint32_t val) {
    uint32_t temp = val + (*ptr);
    return temp;
}


uint32_t callWrapper(uint32_t* ptr) {
    uint32_t b = 3;
    return passPointer(ptr, b);
}


int main() {
    uint32_t __NO_xMR a = 4;

    // temporary fix
    uint32_t __NO_xMR result = callWrapper(&a);
    if (result == 7) {
        printf("Success!\n");
    } else {
        printf("Failure: %d\n", result);
    }

    return 0;
}
