#include"kernel/types.h"
#include"user/user.h"
#include "kernel/stat.h"

int main(){
    int pipe_ptc[2];    //父进程用来给子进程写入信息的管道
    int pipe_ctp[2];    //子进程用来给父进程写入东西的管道
    char* ptc_msg="ping",*ctp_msg="pong",ptc[256],ctp[256];
    if(pipe(pipe_ctp)==-1){
        printf("There is something wrong with pipe()!");
        exit(1);
    }

    if(pipe(pipe_ptc)==-1){
        printf("There is something wrong with pipe()!");
        exit(1);
    }

    if(fork()!=0){//子进程的fork()返回0

        write(pipe_ptc[1],ptc_msg,sizeof(ptc_msg));
        close(pipe_ptc[1]);                                 //关闭父进程管道的写入端
        int parent_pid=getpid();
        wait((int *)0);
        if(read(pipe_ctp[0],ctp,256)!=-1){
            printf("%d: received pong\n",parent_pid);
        }
        close(pipe_ctp[0]);                                 //关闭子进程管道的读入端
        exit(0);
    }
    else{
        int child_pid=getpid();
        
        write(pipe_ctp[1],ctp_msg,sizeof(ctp_msg));
        close(pipe_ctp[1]);                                 //关闭子进程管道的写入端
        
        if(read(pipe_ptc[0],ptc,256)!=-1){
            printf("%d: received ping\n",child_pid);
        }
        close(pipe_ptc[0]);                                 //关闭父进程的读入端

        exit(0);                                            //很重要，要不然子进程不会退出
    }
    exit(0);
}