#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"
#include"kernel/fs.h"
#include"kernel/param.h"

int main(int argc,char *argv[]){
    int i;
    char buf[128]={0};
    int len;
    char* arguments[32];//用来读入命令行的输入
    if(argc==1){
        printf("Usage: xagrs [Command] [para1] ...[para n]");
        exit(1);
    }
    for(i=1;i<argc;i++){
        len=sizeof(argv[i])+1;
        arguments[i-1]=(char *)malloc(sizeof(char)*len);
        strcpy(arguments[i-1],argv[i]);    //不能简单地用“=”来赋值，两者的类型是不一样的
    }
    i--;    //因为这里i已经到了agrc那么大，但实际上arguments数组的下标才记到i-1
    char *p=buf;
    read(0,p,1);
    while(*p){          //苦也，GPT误我！
        if(*p=='\n'){ //如果读到一行末尾
            *p='\0';    //加上'\0'从而构成字符串
            len=sizeof(buf)+1;
            arguments[i]=(char*)malloc(sizeof(char)*len);
            strcpy(arguments[i],buf);
            i++;
            memset(buf,0,128);  //将buf置为初始状态
            p=buf;  //将p指针重新定位到buf开头
            read(0,p,1);
            continue;
        }
        p++;
        read(0,p,1);
    }
    arguments[i]=0;
    if(fork()==0){
        if(exec(argv[1],arguments)==-1){
            printf("xargs: exec failed.\n");
            exit(1);
        }
    }
    wait(0);    //回收子进程
    exit(0);
}