#include <stdio.h>

void __begin_TMR(){}
void __end_TMR(){}

int addNum(int a,int b){
	return a+b;
}

int main(){
	int a = 0;
	int i;

	a++;

	for(i=0;i<10;i++){
		__begin_TMR();
		a = addNum(a,i);
		__end_TMR();
		a+=i;
	}

	a+= 15;

	printf("Done! %d\n\r",a);

	return 0;
}
