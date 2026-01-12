#include <game_launch.hpp>

// May remove later, I dont particularly like this.
static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void mount_overlayfs(std::filesystem::path base_overlay_path, std::filesystem::path path_to_mount) {
    auto upper_path = base_overlay_path / "upper" / std::filesystem::relative(path_to_mount, "/");
    auto work_path = base_overlay_path / "work" / std::filesystem::relative(path_to_mount, "/");
    std::filesystem::create_directories(upper_path);
    std::filesystem::create_directories(work_path);

    std::string opts = 
        "lowerdir="+path_to_mount.string()  + ","
        "upperdir="+upper_path.string()     + ","
        "workdir="+ work_path.string()      + ","
        "userxattr";

    if (mount("overlay", path_to_mount.c_str(), "overlay", 0, opts.c_str()) != 0)
        die(("Failed to mount overlayfs: "+path_to_mount.string()).c_str());
}

static void write_maps(pid_t pid)
{
    char path[128], map[128];
    int fd;

    /* Disable setgroups */
    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    fd = open(path, O_WRONLY);
    if (fd < 0 && errno != ENOENT) die("open setgroups");
    if (fd >= 0) {
        if (write(fd, "deny\n", 5) != 5) die("write setgroups");
        close(fd);
    }

    /* GID map */
    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    fd = open(path, O_WRONLY);
    if (fd < 0) die("open gid_map");
    snprintf(map, sizeof(map), "0 %d 1\n", getgid());
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) die("write gid_map");
    close(fd);

    /* UID map */
    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    fd = open(path, O_WRONLY);
    if (fd < 0) die("open uid_map");
    snprintf(map, sizeof(map), "0 %d 1\n", getuid());
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) die("write uid_map");
    close(fd);
}



void spawn_container_and_game(stateData *data) {
    // int sync_pipe[2];
    // pipe(sync_pipe);
    int sync_pipe_parent[2]; // parent → child
    int sync_pipe_child[2];  // child → parent
    pipe(sync_pipe_parent);
    pipe(sync_pipe_child);

    pid_t child = fork(); // Have parent and child idk

    if (child == -1) throw std::runtime_error("Failed to fork child process");

    if (child == 0) {
        close(sync_pipe_parent[1]);
        close(sync_pipe_child[0]);

        // uid_t uid = getuid();   // real uid outside ns
        // gid_t gid = getgid();   // real gid outside ns

        /* 1. Create user namespace */
        if (unshare(CLONE_NEWUSER) != 0)
            die("unshare user");

        /* 2. Tell parent userns exists */
        write(sync_pipe_child[1], "U", 1);

        /* 3. Wait for mappings */
        char c;
        read(sync_pipe_parent[0], &c, 1);


        // if (setgroups(0, nullptr) != 0)
        //     die("setgroups");
        // if (setresgid(0, 0, 0) != 0)
        //     die("setresgid");
        // if (setresuid(0, 0, 0) != 0)
        //     die("setresuid");


        /* 4. Now mount namespace */
        if (unshare(CLONE_NEWNS) != 0)
            die("unshare mount");


        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);


        // mount_overlayfs("/home/david/ovl", "/home/david");

        mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
        mount("tmpfs", "/var/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
        mount("proc", "/proc", "proc", 0, NULL);
        mount("/dev", "/dev", NULL, MS_BIND | MS_REC, NULL);

        // mount("tmpfs", "/dev/input", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=775");

        // mount("/dev/input/event0", "/dev/input/event0", NULL, MS_BIND, NULL);

        // /* Drop GID */
        // if (setresgid(gid, gid, gid) != 0)
        //     die("setresgid");

        // /* Drop UID */
        // if (setresuid(uid, uid, uid) != 0)
        //     die("setresuid");


        /* 3. Drop ALL supplementary groups (REQUIRED) */
        // if (setgroups(0, nullptr) != 0)
        //     die("setgroups");

        /* 4. Become namespace root */
        // if (setresgid(0, 0, 0) != 0)
        //     die("setresgid");
        // if (setresuid(0, 0, 0) != 0)
        //     die("setresuid");

        system("id");


        // execvp("konsole", NULL);
        char *argv[] = { (char*)"bash", NULL };
        execvp(argv[0], argv);

        die("execvp");
    }

    // close(sync_pipe[0]);
    // write_maps(child);

    close(sync_pipe_parent[0]);
    close(sync_pipe_child[1]);

    /* Wait until child has unshared user namespace */
    char c;
    read(sync_pipe_child[0], &c, 1);

    /* NOW it is legal to write maps */
    write_maps(child);

    /* Allow child to continue */
    write(sync_pipe_parent[1], "M", 1);

    // write(sync_pipe[1], "x", 1);
    // close(sync_pipe[1]);
}