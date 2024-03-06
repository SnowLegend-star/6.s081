#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
//#include "kernel/defs.h" 不可以加这句

int main(int argc,char* argv[]){
    int res=checkmem();
    printf("avaiable memory:%d \nKB, %d MB",res);
    exit(0);
}