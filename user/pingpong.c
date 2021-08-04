#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
    int par[2],chi[2];
    pipe(par);
    pipe(chi);
    char buf1,buf2;

    int pid = fork();
    if (pid > 0)
    { //parent process
        write(par[1], &buf1, 1);
        read(chi[0], &buf2, 1);
        printf("%d: received pong\n", getpid());
    }
    else if (pid == 0)
    { //child process
        read(par[0], &buf2, 1);
        printf("%d: received ping\n", getpid());
        write(chi[1], &buf1, 1);
    }
    exit(0);
}
