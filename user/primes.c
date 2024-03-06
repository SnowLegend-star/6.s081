#include"kernel/types.h"
#include"user/user.h"
#include "kernel/stat.h"

void child(int pipe_p2c[]){
    // int buf[40];                           //buf[0]里面的元素应该是最小的
    int elem,min;                          
    int pipe_s2g[2];                       //子进程给孙进程通信的管道
    close(pipe_p2c[1]);
    // for(i=0;i<40;i++)
    //     buf[i]=0;
    // if(read(pipe_p2c[0],buf,40)==0){   //如果没东西可以读了就可以开始退出了       不能一下子读入sizeof(buf)，因为管道里面的元素没有这么多
    if(read(pipe_p2c[0],&min,sizeof(int))==0){
        close(pipe_p2c[0]);
        exit(0);
    }
    printf("prime %d\n",min);
    pipe(pipe_s2g);
    if(fork()!=0){//子进程准备给孙子进程写东西了
        close(pipe_s2g[0]);                        //把管道的读入端关掉再说
        // for(i=1;buf[i]!=0;i++){
        //     if(buf[i]%buf[0]!=0)                //不能相除的才传给下一辈
        //         write(pipe_s2g[1],&buf[i],1);   //一个字节一个字节地写入管道
        // }
        while(read(pipe_p2c[0],&elem,sizeof(int))!=0){
            if(elem%min!=0)
                write(pipe_s2g[1],&elem,sizeof(int));
        }
        close(pipe_p2c[0]);
        close(pipe_s2g[1]);
        wait(0);
        exit(0);
    }
    else{//孙子进程
        child(pipe_s2g);
    }
}

int main(){
    int i;
    int pipe_p2c[2];
    pipe(pipe_p2c);
    if(fork()!=0){
        close(pipe_p2c[0]);
        for(i=2;i<=35;i++){//把这些数字依次写入管道里面准备让子进程读
            write(pipe_p2c[1],&i,sizeof(int));
        }
        close(pipe_p2c[1]);//及时关闭文件描述符,不然输出会在这里卡住
        wait(0);
        exit(0);        
    }
    else{
        child(pipe_p2c);
    }
    exit(0);
}