#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc >= 2) {
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }

    int p[2];
    if (pipe(p) == -1) {
        fprintf(2, "pipe error!\n");
        exit(1);
    }

    char buf[100];
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error!\n");
        exit(1);
    } else if (pid == 0) {
        read(p[0], buf, sizeof(buf));
        fprintf(1, "%d: received %s\n", getpid(), buf);
        write(p[1], "pong", 4);
        close(p[0]);
        close(p[1]);
        exit(0);
    } else {
        write(p[1], "ping", 4);
        wait((int *)0);
        read(p[0], buf, sizeof(buf));
        fprintf(1, "%d: received %s\n", getpid(), buf);
        close(p[0]);
        close(p[1]);
    }

    exit(0);
}