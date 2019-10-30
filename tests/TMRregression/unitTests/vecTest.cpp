/*
 * vecTest.cpp
 * Basic unit test to exercise using STL containers
 *
 * Notes:
 * Because the STL does all of the memory management for you, all of the "new" and
 *  "delete" operators are hidden inside of what amount to wrapper functions.  These
 *  should be treated the same as malloc() wrapper functions and the calls should
 *  be replicated.  However, they are quite difficult to find.  Function name
 *  mangling turns "new" into "_Znwm" and "delete" into "_ZdlPv".
 *
 * Use the following regex to look for candidates to replicate
 *  "  %call.* = (call|invoke).*@_Z\w*\(.*%\w*.DWC.*$"
 *
 * For this particular test, the following functions must be replicated:
 *  - _ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm
 *  - _ZSt34__uninitialized_move_if_noexcept_aIPiS0_SaIiEET0_T_S3_S2_RT1_
 *
 * When compiled with -noMemReplication, also add the following options:
 *  - ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim
 * this gets rid of a double free() error
 */

#include <iostream>
#include <vector>

#define SIZE 4

int main() {
    std::vector<int> vec;
    for (int i = 0; i < SIZE; i+=1) {
        vec.push_back(i);
    }

    std::size_t vsize = vec.size();
    std::cout << "vector size: " << vsize << "\n";

    //check
    if (vsize == SIZE) {
        return 0;
    } else {
        return -1;
    }

}
