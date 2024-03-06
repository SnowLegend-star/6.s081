#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"
#include"kernel/fs.h"

char *fmtname(char *path){
    static char buf[DIRSIZ+1];
    char *p;

    //find first character after last slash
    for(p=path+strlen(path);p>path&&*p!='/';p--);

    p++;

    //return blank-padded name
    if(strlen(p)>=DIRSIZ)
        return p;
    memmove(buf,p,strlen(p));
    memset(buf+strlen(p),' ',DIRSIZ-strlen(p));
    return buf;
}

int flag=0;
int dir_ItemNum=0;    //看一下当前目录底下有多少条信息 “.”和“..”没被统计进去

void find(char *directory,char *filename){
    char buf[512],*p;       //p作为定位指针
    int fd;
    struct dirent de;
    struct stat st;
    if((fd=open(directory,0))<0){
        printf("find: cannot open %s\n",directory);
        exit(1);
    }

    if(fstat(fd,&st)<0){
        printf("find: cannot stat %s\n",directory);
        close(fd);
        exit(1);
    }

    struct stat stat_temp;  //这句话不能定义在case内部吗？    

    switch(st.type){
        case T_DEVICE:
        case T_FILE:
            if(strcmp(fmtname(directory),filename)==0){
                printf("%s\n",directory);
                flag=1;
            }
            exit(0);

        case T_DIR:
            if(strlen(directory)+1+DIRSIZ+1>sizeof(buf)){
                printf("find: path too long\n");
                break;
            }

            strcpy(buf,directory);              //buf用来存当前正在访问目录的路径
            p=buf+strlen(buf);
            *p++='/';                           //把p挪到buf的最后一个元素上
            while(read(fd,&de,sizeof(de))==sizeof(de)){
                // dir_ItemNum++;  //怎么把这句放在if(d.inum)前面只能输出64呢？                
                if(de.inum==0)                  //如果 inum 字段等于 0，通常表示该目录项无效或未使用
                    continue ;

                memmove(p,de.name,DIRSIZ);      //把文件名都存在buf刚添上的“/”后面
                p[DIRSIZ]=0;
                if(stat(buf,&stat_temp)<0){     //这不是多此一举吗  有用的，可以判断当前目录下打开的文件是什么类型
                    printf("find: cannot stat %s\n",buf);
                    continue;
                }
                if(stat_temp.type==T_FILE)      //如果是文件类型
                    if(strcmp(de.name,filename)==0){
                        printf("%s\n",buf);
                        flag=1;                        
                    }

                if(stat_temp.type==T_DIR){
                    if((strcmp(de.name,".")==0)||(strcmp(de.name,"..")==0))     //得排除掉“.”和“..”这两个目录防止无限递归
                        continue ;
                    find(buf,filename);         //递归访问这个目录 开始把buf写成de.name了，有点小丑                   
                }

            }
    }
    return ;
}

int main(int argc,char *argv[]){
    if(argc!=3){
        printf("Usage: find <directory> <filename>\n");
        exit(1);
    }
    find(argv[1],argv[2]);
    printf("%d\n",dir_ItemNum);
    if(flag==0)
        printf("Fail to find the file '%s'!\n",argv[2]);
    exit(0);
}