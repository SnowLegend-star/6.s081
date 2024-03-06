#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[]) {


    char buf[256];
    int p[2];
    pipe(p);
    int pid = fork();
    if (pid) {
        close(p[0]);
        write(p[1], "hhh", 3);
        close(p[1]);
        exit(0);
    } else {
        close(p[1]);
        int bytes_read = read(p[0], buf, sizeof(buf));
        if (bytes_read > 0) {
            buf[bytes_read] = '\0'; // 添加字符串结束符
            printf("successfully received : %s\n", buf);
        }
        close(p[0]);
        exit(0);
    }
}
