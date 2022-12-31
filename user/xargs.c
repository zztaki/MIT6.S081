#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void getLine(char *buf, int max) {
    int i, cc;
    char c;

    for (i = 0; i + 1 < max; i++) {
        cc = read(0, &c, 1);
        if (cc < 1 || c == '\n' || c == '\r')
            break;
        buf[i] = c;
    }
    buf[i] = '\0';
}

int main(int argc, char *argv[]) {
    char *Argv[MAXARG], buf[101];
    int idx;
    for (idx = 0; idx < argc - 1; idx++) {
        Argv[idx] = argv[idx + 1]; // argv[0]是 "xargs"
    }
    getLine(buf, 100); // 得到一行参数
    while (strlen(buf)) {
        Argv[idx] = buf;
        Argv[idx + 1] = 0;

        int pid = fork();
        if (pid < 0) {
            fprintf(2, "fork error!\n");
            exit(1);
        } else if (pid == 0) {
            exec(Argv[0], Argv);
            fprintf(2, "exec error!\n");
            exit(1);
        }

        wait((int *)0);
        getLine(buf, 100);
    }
    exit(0);
}