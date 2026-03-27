#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

// Global state to store our branch paths
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/**
 * Helper to resolve paths based on UnionFS logic
 */
int resolve_path(const char *path, char *resolved_path) {
    char upper_p[1024], lower_p[1024], whiteout_p[1024];
    struct stat st;

    sprintf(upper_p, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_p, "%s%s", UNIONFS_DATA->lower_dir, path);
    
    // Generate whiteout path (e.g., .wh.filename)
    char *slash = strrchr(path, '/');
    if (slash) {
        sprintf(whiteout_p, "%s%.*s/.wh.%s", UNIONFS_DATA->upper_dir, (int)(slash - path), path, slash + 1);
    } else {
        sprintf(whiteout_p, "%s/.wh.%s", UNIONFS_DATA->upper_dir, path);
    }

    // 1. Check Whiteout
    if (lstat(whiteout_p, &st) == 0) return -ENOENT;

    // 2. Check Upper
    if (lstat(upper_p, &st) == 0) {
        strcpy(resolved_path, upper_p);
        return 0;
    }

    // 3. Check Lower
    if (lstat(lower_p, &st) == 0) {
        strcpy(resolved_path, lower_p);
        return 0;
    }

    return -ENOENT;
}

// Get file attributes
static int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    char r_path[1024];
    int res = resolve_path(path, r_path);
    if (res != 0) return res;

    res = lstat(r_path, stbuf);
    if (res == -1) return -errno;
    return 0;
}

// readdir is required to actually see files
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // For now, this is a placeholder. 
    // You will later add code here to loop through actual directories.
    return 0;
}

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
};

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <lower_dir> <upper_dir> <mountpoint>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    // Basic FUSE setup: mountpoint is the 3rd argument
    char *fuse_argv[argc];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3]; // The mount point
    for(int i = 4; i < argc; i++) fuse_argv[i-2] = argv[i];

    return fuse_main(argc - 2, fuse_argv, &unionfs_oper, state);
}
