/*
 * signalHandlers.c
 * 
 * This unit test is designed to test
 *  1) how COAST works with signal handlers
 *  2) if we can mark functions as an ISR and COAST will leave them alone
 *
 * If the program doesn't stop with Ctrl+C, then find the PID and send the
 *  signal directly to it, like
 * `kill -s SIGINT 12345`
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// COAST configuration
#include "../../COAST.h"
__DEFAULT_xMR


// print something and exit
// this shouldn't be touched by COAST at all
void SIGINT_handler(int sig_num) __ISR_FUNC {
    // Can only call functions which are safe for signal handlers
    // https://stackoverflow.com/a/12902707/12940429
    char msg[] = "\nCaught Ctrl-C\nExiting gracefully...\n";
    write(STDOUT_FILENO, msg, sizeof(msg));
    _exit(1);
}


// this function will never return, just do math operations forever
void doMath(void) __xMR {
    int x = 0, y = 1, z = -1;

    while (1) {
        // this should be fun
        x += y;
        y ^= z;
        z *= x;
    }
}


int main(void) {
    // register handler
    signal(SIGINT, SIGINT_handler);

    // do some math forever
    doMath();

    return 0;
}