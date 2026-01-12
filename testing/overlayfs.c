#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void write_map(pid_t pid, const char *file, const char *data)
{
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/%s", pid, file);

    int fd = open(path, O_WRONLY);
    if (fd < 0) die(path);

    if (write(fd, data, strlen(data)) < 0)
        die("write map");

    close(fd);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    pid_t child = fork();
    if (child < 0)
        die("fork");

    if (child == 0) {
        /* CHILD */

        if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0)
            die("unshare");

        /* Wait for parent to write uid/gid maps */
        char dummy;
        read(STDIN_FILENO, &dummy, 1);

        /* Mount propagation safety */
        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
            die("mount private");

        /* Overlay setup */
        const char *home = getenv("HOME");
        char upper[512], work[512], merged[512];

        snprintf(upper,  sizeof(upper),  "%s/ovl/upper", home);
        snprintf(work,   sizeof(work),   "%s/ovl/work", home);
        snprintf(merged, sizeof(merged), "%s/ovl/merged", home);

        mkdir(upper, 0755);
        mkdir(work, 0755);
        mkdir(merged, 0755);

        char opts[2048];
        snprintf(opts, sizeof(opts),
            "lowerdir=/,upperdir=%s,workdir=%s", upper, work);

        if (mount("overlay", merged, "overlay", 0, opts) != 0)
            die("overlay mount");

        mkdir(strcat(strcpy(opts, merged), "/dev"), 0755);
        mkdir(strcat(strcpy(opts, merged), "/proc"), 0755);

        mount("/dev", strcat(strcpy(opts, merged), "/dev"),
              NULL, MS_BIND | MS_REC, NULL);

        mount("proc", strcat(strcpy(opts, merged), "/proc"),
              "proc", 0, NULL);

        if (chdir(merged) != 0) die("chdir merged");
        if (chroot(".") != 0) die("chroot");
        if (chdir("/") != 0) die("chdir /");

        execvp(argv[1], &argv[1]);
        die("execvp");
    }

    /* PARENT */

    char map[128];

    /* Required order */
    write_map(child, "setgroups", "deny\n");

    snprintf(map, sizeof(map), "0 %d 1\n", getuid());
    write_map(child, "uid_map", map);

    snprintf(map, sizeof(map), "0 %d 1\n", getgid());
    write_map(child, "gid_map", map);

    /* Signal child to continue */
    write(STDOUT_FILENO, "x", 1);

    waitpid(child, NULL, 0);
    return 0;
}
