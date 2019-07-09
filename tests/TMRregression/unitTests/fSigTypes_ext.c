//External function definitions for
// fSigTypes.c

//global variable
int x = 0;

// type 1
void inc(int i) {
    i += 1;
    return;
}

// type 2
int add(int i, int j) {
    return (i + j);
}

// type 3
void incx() {
    x += 1;
    return;
}

// type 4
int test(){
    return (x > 0);
}
