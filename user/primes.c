#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void printPrime(int *input, int cnt) {
    if (cnt == 0) {
        return;
    }

    int p[2];
    if (pipe(p) == -1) {
        fprintf(2, "pipe error!\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error!\n");
        exit(1);
    } else if (pid == 0) {
        close(p[1]);
        int output[34], idx = 0;

        while (read(p[0], &output[idx], 4)) {
            idx++;
        }
        close(p[0]);
        printPrime(output, idx);
        exit(0);

    } else {
        close(p[0]);
        fprintf(1, "prime %d\n", input[0]);
        for (int i = 1; i < cnt; i++) {
            if (input[i] % input[0] != 0) {
                write(p[1], (char *)(input + i), 4);
            }
        }
        close(p[1]);
        wait((int *)0);
    }
}

int main(int argc, char *argv[]) {
    if (argc >= 2) {
        fprintf(2, "Usage: primes\n");
        exit(1);
    }

    int input[34];
    for (int i = 0; i < 34; i++) {
        input[i] = i + 2;
    }
    printPrime(input, 34);
    exit(0);
}