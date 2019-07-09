/*
 * Example uses of the <ctime.h> library
 * to make sure we know how to treat them
 *
 * base program copied from
 * http://www.cplusplus.com/reference/ctime/
 * examples for the various functions
 *
 * make sure to add clock() to -skipLibCalls
 */

#include <stdio.h>
#include <time.h>

int main() {
    clock_t begin;
    begin = clock();

    time_t rawTime;

    time(&rawTime);
    printf ("Using time and ctime: %s", ctime (&rawTime));

    struct tm * timeInfo;
    timeInfo = localtime(&rawTime);
    printf ( "Using localtime and asctime: %s", asctime (timeInfo) );

    //set the time to the beginning of the day
    timeInfo->tm_hour = 0; timeInfo->tm_min = 0; timeInfo->tm_sec = 0;
    double seconds = difftime(rawTime, mktime(timeInfo));

    printf("Using difftime and mktime: ");
    printf("%f seconds since today started\n",seconds);

    //gmtime and strftime
    struct tm * ptm;
    ptm = gmtime(&rawTime);
    char buffer [80];
    strftime(buffer, 80, "GMT - %R", ptm);
    printf("Using gmtime and strftime: %s\n", buffer);

    clock_t end;
    end = clock();
    clock_t dur = end-begin;

    printf("Using clock: %ld clicks to run (%f seconds)\n", dur, ((float)dur)/CLOCKS_PER_SEC);

    return 0;
}
