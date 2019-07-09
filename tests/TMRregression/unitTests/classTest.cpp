/*
 * classTest.cpp
 * Basic unit test to exercise class constructors and destructors
 */

#include <iostream>

class myClass {
private:
    int stuff;

public:
    myClass();
    ~myClass();
    int getStuff();
};

myClass::myClass() {
    stuff = 0;
    return;
}

myClass::~myClass() {
    return;
}

int myClass::getStuff() {
    return this->stuff;
}

int main() {
    myClass mc;
    int x = mc.getStuff();
    std::cout << "stuff = " << x << "\n";

    return 0;
}
