/*
 * halfProtected.c
 *
 * This test designed to show what it's like when protecting a top layer
 *  of functions, leaving some bottom "system calls" alone.
 * One of the problems is in properly getting the return values from
 *  function calls.
 *
 * Need a call to function with one of the args specified NO_xMR,
 *  and another with just strings or constant args.
 * 
 * NOTE: these functions definitely aren't completely safe for all possible
 *  arguments. Doesn't matter, just need one call.
 */


/********************************** Includes **********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "COAST.h"


/******************************** Definitions *********************************/
#define BUF_SIZE 16

#define FAKE_FILE_SIZE 64
char __NO_xMR fakeFile[FAKE_FILE_SIZE];


/********************************* Functions **********************************/
// writes bytesToWrite to file
// returns number of bytes written
__NO_xMR
int fakeFileWrite(char* buffer, int bytesToWrite) {
    int numWritten;

    for (numWritten = 0;
         (numWritten < bytesToWrite) && (numWritten < FAKE_FILE_SIZE);
         numWritten++)
    {
        fakeFile[numWritten] = buffer[numWritten];
    }

    return numWritten;
}


__NO_xMR
int doSomeMath(const char* mathStr, int addNum) {
    int x = atoi(mathStr);
    return x + addNum;
}


// This function checks that calls to library functions without
//  any replicated args will not be replicated.
void checkCloneFnCall() {
    char __NO_xMR tempBuff[BUF_SIZE];
    memset(tempBuff, 0, BUF_SIZE);
    printf("tempBuf[4] = %hhu\n", tempBuff[4]);
    return;
}


int main() {
    // declare variables
    int numRet, status;
    // create a buffer that isn't protected
    char* __NO_xMR buffer;

    // set up string buffer
    buffer = malloc(BUF_SIZE);
    snprintf(buffer, BUF_SIZE, "hello there");

    // use it in call
    numRet = fakeFileWrite(buffer, strlen(buffer));
    // expected: 11
    if (numRet != 11) {
        printf("ERROR: num bytes written = %d\n", numRet);
        return -1;
    }

    // do math call
    numRet = doSomeMath("4", 5);
    // expected: 9
    if (numRet != 9) {
        printf("ERROR: result = %d\n", numRet);
        return -1;
    }

    // this one isn't self-checking
    checkCloneFnCall();

    printf("Success!\n");
    return 0;
}
