#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "logrotate.h"


#define MAX_SYMLINKS  5

/**
 * Returns CHK_SECURE     if directory is secure,
 *         CHK_ERROR      on error,
 *         CHK_NOTDIR     if a part of the path is not a directory,
 *         CHK_FAIL_OWNER if a (parent) directory is owned by someone besides user and root,
 *         CHK_FAIL_GROUP if a (parent) directory is writable by group except the root group,
 *         CHK_FAIL_OTHER if a (parent) directory is writable by others
 *
 * adopted from https://wiki.sei.cmu.edu/confluence/display/c/FIO15-C.+Ensure+that+file+operations+are+performed+in+a+secure+directory
 */
int check_dir_secure(const char *fullpath) {
    static unsigned num_symlinks = 0;
    char **dirs;
    int num_of_dirs = 1;
    int secure = CHK_SECURE;
    int i;
    struct stat buf;
    uid_t my_uid = geteuid();

    if (!fullpath || fullpath[0] != '/') {
        message(MESS_ERROR, "invalid directory %s to check\n", fullpath ? fullpath : "()");
        return CHK_ERROR;
    }

    if (num_symlinks > MAX_SYMLINKS) {
        message(MESS_ERROR, "directory %s has too many parent symlinks\n", fullpath);
        return CHK_ERROR;
    }

    {
        char *path_parent;
        char *path_copy = strdup(fullpath);

        if (path_copy == NULL) {
            message(MESS_ERROR, "can not allocate memory\n");
            return CHK_ERROR;
        }

        /* Figure out how far it is to the root */
        for (path_parent = path_copy; ((strcmp(path_parent, "/") != 0) &&
                (strcmp(path_parent, "//") != 0) &&
                (strcmp(path_parent, ".") != 0));
            path_parent = dirname(path_parent)) {
            num_of_dirs++;
        } /* Now num_of_dirs indicates # of dirs we must check */
        free(path_copy);
    }

    dirs = malloc(num_of_dirs * sizeof(char *));
    if (dirs == NULL) {
        message(MESS_ERROR, "can not allocate memory\n");
        return CHK_ERROR;
    }

    dirs[num_of_dirs - 1] = strdup(fullpath);
    if (dirs[num_of_dirs - 1] == NULL) {
        message(MESS_ERROR, "can not allocate memory\n");
        free(dirs);
        return CHK_ERROR;
    }

    {
        char *path_parent;
        char *path_copy = strdup(fullpath);

        if (path_copy == NULL) {
            message(MESS_ERROR, "can not allocate memory\n");
            free(dirs);
            return CHK_ERROR;
        }

        /* Now fill the dirs array */
        path_parent = path_copy;
        for (i = num_of_dirs - 2; i >= 0; i--) {
            path_parent = dirname(path_parent);
            dirs[i] = strdup(path_parent);
            if (dirs[i] == NULL) {
                int j;

                message(MESS_ERROR, "can not allocate memory\n");
                for (j = num_of_dirs - 1; j > i; j--) {
                    free(dirs[j]);
                }
                free(path_copy);
                free(dirs);
                return CHK_ERROR;
            }
        }
        free(path_copy);
    }

    /*
     * Traverse from the root to the fullpath,
     * checking permissions along the way.
     */
    for (i = 0; i < num_of_dirs; i++) {
        if (lstat(dirs[i], &buf) != 0) {
            message(MESS_ERROR, "can not stat directory %s: %s\n", dirs[i], strerror(errno));
            secure = CHK_ERROR;
            break;
        }

        if (S_ISLNK(buf.st_mode)) { /* Symlink, test linked-to file */
            size_t linksize = buf.st_size + 1;
            ssize_t r;
            int rv;
            char *link_path = malloc(linksize);

            if (link_path == NULL) {
                message(MESS_ERROR, "can not allocate memory\n");
                secure = CHK_ERROR;
                break;
            }

            r = readlink(dirs[i], link_path, linksize);
            if (r < 0) {
                message(MESS_ERROR, "can not readlink %s: %s\n", dirs[i], strerror(errno));
                secure = CHK_ERROR;
                free(link_path);
                break;
            } else if ((size_t)r >= linksize) {
                message(MESS_ERROR, "link destination of %s too long (%lu/%lu): %s\n", dirs[i], (size_t)r, linksize, strerror(errno));
                secure = CHK_ERROR;
                free(link_path);
                break;
            }
            link_path[r] = '\0';

            num_symlinks++;
            rv = check_dir_secure(link_path);
            num_symlinks--;

            free(link_path);

            if (rv != 0) {
                secure = rv;
                break;
            }

            continue;
        }

        if (!S_ISDIR(buf.st_mode)) { /* Not a directory */
            secure = CHK_NOTDIR;
            break;
        }

        if ((buf.st_uid != my_uid) && (buf.st_uid != 0)) {
            /* Directory is owned by someone besides user or root */
            secure = CHK_FAIL_OWNER;
            break;
        }

        if (buf.st_gid != 0 && (buf.st_mode & S_IWGRP)) { /* dir is writable by group */
            secure = CHK_FAIL_GROUP;
            break;
        }

        if (buf.st_mode & S_IWOTH) { /* dir is writable by everyone */
            secure = CHK_FAIL_OTHER;
            break;
        }
    }

    for (i = 0; i < num_of_dirs; i++) {
        free(dirs[i]);
    }
    free(dirs);

    return secure;
}

#define BUF_LEN 1024

/**
 * Returns CHK_SECURE     if filepath is inside a secure directory,
 *         CHK_ERROR      on error,
 *         CHK_NOTDIR     if a part of the path is not a directory,
 *         CHK_FAIL_OWNER if a parent directory is owned by someone besides user and root,
 *         CHK_FAIL_GROUP if a parent directory is writable by group except the root group,
 *         CHK_FAIL_OTHER if a parent directory is writable by others
 */
int check_file_in_secure_dir(const char *filepath) {
    char buf[BUF_LEN];

    if (filepath == NULL) {
        message(MESS_ERROR, "invalid file to check\n");
        return CHK_ERROR;
    }

    if (filepath[0] == '/') {
        int rv;
        char *path_parent;
        char *path_copy = strdup(filepath);

        if (path_copy == NULL) {
            message(MESS_ERROR, "can not allocate memory\n");
            return CHK_ERROR;
        }

        path_parent = dirname(path_copy);

        rv = check_dir_secure(path_parent);

        free(path_copy);

        return rv;
    }

    if (getcwd(buf, BUF_LEN) == NULL) {
        message(MESS_ERROR, "can not get current directory name: %s\n", strerror(errno));
        return CHK_ERROR;
    }

    return check_dir_secure(buf);
}

/**
 * Returns CHK_SECURE     if path is a secure directory or a file inside one,
 *         CHK_ERROR      on error,
 *         CHK_NOTDIR     if a part of the path is not a directory,
 *         CHK_FAIL_OWNER if a (parent) directory is owned by someone besides user and root,
 *         CHK_FAIL_GROUP if a (parent) directory is writable by group except the root group,
 *         CHK_FAIL_OTHER if a (parent) directory is writable by others
 */
int check_path_secure(const char *path) {
    struct stat sb;

    if (stat(path, &sb) == -1) {
        message(MESS_ERROR, "can not stat %s: %s\n", path, strerror(errno));
        return CHK_ERROR;
    }

    if (S_ISDIR(sb.st_mode)) {
        return check_dir_secure(path);
    }

    return check_file_in_secure_dir(path);
}
