/*
 * exceptions.cpp
 * Unit test to exercise some basic c++ exceptions
 * https://www.tutorialspoint.com/cplusplus/cpp_exceptions_handling.htm
 *
 * need to add
 * -replicateFnCalls=_ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm,_ZSt27__uninitialized_default_n_aIPimiET_S1_T0_RSaIT1_E
 * to -DWC or -TMR invocation
 *
 * When compiled with -noMemReplication, also add the following options:
 *  - ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim
 * this gets rid of a double free() error
 */

#include <iostream>
#include <stdexcept>
#include <vector>


double division(int a, int b) {
    if( b == 0 ) {
        throw "Division by zero condition!";
    }
    return (a/b);
}

double multiplication(int a, int b) {
    return a * b;
}

int main () {
    int x = 50;
    int y = 0;
    double z = 0;
    int exceptionCount = 0;

    double m = multiplication(x, y);

    //example of a user-defined function which throws an exception
    try {
        z = division(x, y);
        std::cout << z << std::endl;
    } catch (const char* msg) {
        std::cerr << msg << std::endl;
        exceptionCount+=1;
    }

    //example of a library function which throws an exception
    // http://www.cplusplus.com/reference/stdexcept/out_of_range/
    std::vector<int> myvector(10);
    try {
        myvector.at(20)=100;      // vector::at throws an out-of-range
    } catch (const std::out_of_range& oor) {
        std::cerr << "Out of Range error: " << oor.what() << '\n';
        exceptionCount+=1;
    }

    //expected results
    if (exceptionCount == 2) {
        printf("Success!\n");
        return 0;
    } else {
        printf("Error!\n");
        return -1;
    }
}
