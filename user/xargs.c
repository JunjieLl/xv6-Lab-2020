#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Missing parameters\n");
        exit(1);
    }

    char param[MAXARG][MAXARG];//保存xargc的参数以及来自标准输入的参数
    char *xargv[MAXARG];//指向param，传递给exec

    //参数数组指向对应的参数string
    for (int i = 0; i < MAXARG; i++)
    {
        xargv[i] = param[i];
    }
    //复制已有参数
    for (int i = 1; i < argc; i++)
    {
        strcpy(param[i-1], argv[i]);//-1是因为需要的是执行的命令,而非xargc本身
    }

    char buf[512];
    int index = 0, xargc = argc-1;
    //从标准输入中继续读入参数
    while (read(0, buf + index, 1) == 1)
    { //从标准输入中逐个读入字符
        if (buf[index] == '\n')
        { //一行结束
            buf[index] = '\0';
            strcpy(param[xargc], buf);
            ++xargc;
            xargv[xargc] = 0;
            if (fork() == 0)
            {
                exec(xargv[0], xargv);
            }
            else
            {
                wait(0);
            }
            //执行完后恢复到xargc命令的正常参数末尾
            xargv[xargc] = param[xargc];//恢复指向param
            xargc = argc-1;
            index = 0;
        }
        else if (buf[index] == ' ')
        {
            buf[index]='\0';
            strcpy(param[xargc], buf);
            ++xargc;
            index = 0;
        }
        else
        {
            ++index;
        }
    }

    exit(0);
}
