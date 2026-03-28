#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
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
    (void) offset; (void) fi; (void) flags;

    char upper_p[1024], lower_p[1024];
    sprintf(upper_p, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_p, "%s%s", UNIONFS_DATA->lower_dir, path);

    // We use a simple approach: fill from upper, then fill from lower
    // Note: A professional version would use a hash table to track duplicates/whiteouts
    
    // 1. Read from Upper Directory
    DIR *dp = opendir(upper_p);
    if (dp != NULL) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            // Skip whiteout metadata files from the listing
            if (strncmp(de->d_name, ".wh.", 4) == 0) continue;
            
            if (filler(buf, de->d_name, NULL, 0, 0)) break;
        }
        closedir(dp);
    }

    // 2. Read from Lower Directory
    dp = opendir(lower_p);
    if (dp != NULL) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            // Check if this file is already in the upper_dir (Precedence)
            char check_upper[1024];
            sprintf(check_upper, "%s/%s", upper_p, de->d_name);
            
            char check_wh[1024];
            sprintf(check_wh, "%s/.wh.%s", upper_p, de->d_name);

            struct stat st;
            // Only add if it doesn't exist in upper AND isn't whiteouted
            if (lstat(check_upper, &st) != 0 && lstat(check_wh, &st) != 0) {
                if (filler(buf, de->d_name, NULL, 0, 0)) break;
            }
        }
        closedir(dp);
    }

    return 0;
}

static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char upper_path[1024], lower_path[1024];
    struct stat st;

    sprintf(upper_path, "%s%s", UNIONFS_DATA->upper_dir, path);
    sprintf(lower_path, "%s%s", UNIONFS_DATA->lower_dir, path);

    //CHECK: Is file opened in WRITE mode?
    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)) {

        //CASE: File exists ONLY in lower → trigger CoW
        if (access(upper_path, F_OK) != 0 && access(lower_path, F_OK) == 0) {

            // --- COPY lower → upper ---
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

    //Now open from resolved path
    char resolved_path[1024];
    int res = resolve_path(path, resolved_path);
    if (res != 0) return res;

    int fd = open(resolved_path, fi->flags);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

static int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    int fd = fi->fh;

    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    return res;
}

static int unionfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh;

    int res = pwrite(fd, buf, size, offset);
    if (res == -1) res = -errno;

    return res;
}

static int unionfs_release(const char *path, struct fuse_file_info *fi) {
    close(fi->fh);
    return 0;
}

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .release = unionfs_release,
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
