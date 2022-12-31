#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void findName(char *path, char *name) {

    int fd;
    char *p, buf[100];
    struct stat st;
    struct dirent de;
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
    case T_DEVICE:
        for (p = path + strlen(path); p >= path && *p != '/'; p--)
            ;
        p++;
        if (!strcmp(p, name)) {
            fprintf(1, "%s\n", path);
        }
        break;

    case T_DIR:
        memmove(buf, path, strlen(path));
        p = buf + strlen(path);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0 || !strcmp(de.name, ".") || !strcmp(de.name, ".."))
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0) {
                fprintf(2, "ls: cannot stat %s\n", buf);
                p = buf + strlen(path) + 1;
                continue;
            }
            findName(buf, name);
            p = buf + strlen(path) + 1;
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "Usage: find <path> <name>\n");
        exit(1);
    }
    char *path = argv[1], *name = argv[2];
    findName(path, name);

    exit(0);
}