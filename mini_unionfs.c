#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

// Global state
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/**
 * Path resolution
 */
int resolve_path(const char *path, char *resolved_path) {
    char upper_p[1024], lower_p[1024], whiteout_p[1024];
    struct stat st;

    sprintf(upper_p, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_p, "%s%s", UNIONFS_DATA->lower_dir, path);

    char *slash = strrchr(path, '/');
    if (slash) {
        sprintf(whiteout_p, "%s%.*s/.wh.%s", UNIONFS_DATA->upper_dir,
                (int)(slash - path), path, slash + 1);
    } else {
        sprintf(whiteout_p, "%s/.wh.%s", UNIONFS_DATA->upper_dir, path);
    }

    // Whiteout check
    if (lstat(whiteout_p, &st) == 0) return -ENOENT;

    // Upper
    if (lstat(upper_p, &st) == 0) {
        strcpy(resolved_path, upper_p);
        return 0;
    }

    // Lower
    if (lstat(lower_p, &st) == 0) {
        strcpy(resolved_path, lower_p);
        return 0;
    }

    return -ENOENT;
}

// getattr
static int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    char r_path[1024];
    int res = resolve_path(path, r_path);
    if (res != 0) return res;

    res = lstat(r_path, stbuf);
    if (res == -1) return -errno;
    return 0;
}

// readdir
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset; (void) fi; (void) flags;

    char upper_p[1024], lower_p[1024];
    sprintf(upper_p, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_p, "%s%s", UNIONFS_DATA->lower_dir, path);

    DIR *dp = opendir(upper_p);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (strncmp(de->d_name, ".wh.", 4) == 0) continue;
            if (filler(buf, de->d_name, NULL, 0, 0)) break;
        }
        closedir(dp);
    }

    dp = opendir(lower_p);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            char check_upper[1024], check_wh[1024];
            sprintf(check_upper, "%s/%s", upper_p, de->d_name);
            sprintf(check_wh, "%s/.wh.%s", upper_p, de->d_name);

            struct stat st;
            if (lstat(check_upper, &st) != 0 && lstat(check_wh, &st) != 0) {
                if (filler(buf, de->d_name, NULL, 0, 0)) break;
            }
        }
        closedir(dp);
    }

    return 0;
}

// open (with CoW)
static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char upper_path[1024], lower_path[1024];
    struct stat st;

    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_path, "%s%s", UNIONFS_DATA->lower_dir, path);

    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)) {
        if (access(upper_path, F_OK) != 0 && access(lower_path, F_OK) == 0) {
            int src = open(lower_path, O_RDONLY);
            if (src < 0) return -errno;

            int dest = open(upper_path, O_WRONLY | O_CREAT, 0644);
            if (dest < 0) {
                close(src);
                return -errno;
            }

            char buffer[4096];
            ssize_t bytes;

            while ((bytes = read(src, buffer, sizeof(buffer))) > 0) {
                write(dest, buffer, bytes);
            }

            close(src);
            close(dest);
        }
    }

    char resolved_path[1024];
    int res = resolve_path(path, resolved_path);
    if (res != 0) return res;

    int fd = open(resolved_path, fi->flags);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

// read
static int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) res = -errno;
    return res;
}

// write
static int unionfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    int res = pwrite(fi->fh, buf, size, offset);
    if (res == -1) res = -errno;
    return res;
}

// release
static int unionfs_release(const char *path, struct fuse_file_info *fi) {
    close(fi->fh);
    return 0;
}

// unlink (whiteout)
static int unionfs_unlink(const char *path) {
    char upper_path[1024], lower_path[1024], whiteout_path[1024];

    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_path, "%s%s", UNIONFS_DATA->lower_dir, path);

    char *slash = strrchr(path, '/');
    if (slash) {
        sprintf(whiteout_path, "%s%.*s/.wh.%s",
                UNIONFS_DATA->upper_dir,
                (int)(slash - path),
                path,
                slash + 1);
    } else {
        sprintf(whiteout_path, "%s/.wh.%s",
                UNIONFS_DATA->upper_dir,
                path);
    }

    if (access(upper_path, F_OK) == 0) unlink(upper_path);

    if (access(lower_path, F_OK) == 0) {
        FILE *wh = fopen(whiteout_path, "w");
        if (!wh) return -errno;
        fclose(wh);
    }

    return 0;
}

// create
static int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char upper_path[1024];
    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);

    int fd = open(upper_path, fi->flags | O_CREAT, mode);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

// mkdir
static int unionfs_mkdir(const char *path, mode_t mode) {
    char upper_path[1024];
    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);

    int res = mkdir(upper_path, mode);
    if (res == -1) return -errno;

    return 0;
}

// rmdir
static int unionfs_rmdir(const char *path) {
    char upper_path[1024], lower_path[1024], whiteout_path[1024];

    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_path, "%s%s", UNIONFS_DATA->lower_dir, path);

    char *slash = strrchr(path, '/');
    if (slash) {
        sprintf(whiteout_path, "%s%.*s/.wh.%s",
                UNIONFS_DATA->upper_dir,
                (int)(slash - path),
                path,
                slash + 1);
    } else {
        sprintf(whiteout_path, "%s/.wh.%s",
                UNIONFS_DATA->upper_dir,
                path);
    }

    if (access(upper_path, F_OK) == 0) {
        if (rmdir(upper_path) != 0) return -errno;
    }

    if (access(lower_path, F_OK) == 0) {
        FILE *wh = fopen(whiteout_path, "w");
        if (!wh) return -errno;
        fclose(wh);
    }

    return 0;
}

// operations
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .release = unionfs_release,
    .unlink  = unionfs_unlink,
    .create  = unionfs_create,
    .mkdir   = unionfs_mkdir,
    .rmdir   = unionfs_rmdir,
};

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <lower_dir> <upper_dir> <mountpoint>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    char *fuse_argv[argc];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3];

    for(int i = 4; i < argc; i++) fuse_argv[i-2] = argv[i];

    return fuse_main(argc - 2, fuse_argv, &unionfs_oper, state);
}
