#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

#include "trustdir.h"
#include "log.h"

#ifdef WITH_ACL
#include "sys/acl.h"
#define acl_type acl_t
#else
#define acl_type void *
#endif


#define FREE(p)   do { free(p); (p) = NULL; } while(0)
 
static int linkcount;


/* returns numbers of nodes in directory-path */
static int get_directory_num (const char* path) {
    int i = 0;
    char *p, *cp ;

    cp = strdup(path);
    if ( !cp ) {
        message(MESS_ERROR,"strdup of %s failed: %s\n",path,strerror(errno));
        return -1;
    }

    p = cp;

    while( (strcmp(p, "/") != 0) &&
           (strcmp(p, "//") != 0) &&
           (strcmp(p, ".") != 0) ) {
        p = dirname(p);
        i++;
    }

    FREE(cp);

    return i;
}

static char** fill_dir_array (const char* path,int *numDirs) {
    char *tmp;
    int i = 0;
    char **dirArray;

    if (numDirs == NULL) {
        message(MESS_ERROR,"Invalid Argument\n");
        return NULL;
    }

    *numDirs = get_directory_num(path);
    if (*numDirs < 0)
        return NULL;

    if (*numDirs == 0)
        return NULL;

    dirArray = (char**)malloc(*numDirs * sizeof(char*));
    if (dirArray == NULL) {
        message(MESS_ERROR,"could not allocate memory for "
                "array of directories\n");
        return NULL;
    }

    dirArray[*numDirs-1] = strdup(path);
    if (dirArray[*numDirs-1] == 0) {
        message(MESS_ERROR,"strdup of %s failed: %s\n",path,strerror(errno));
        FREE(dirArray);
        return NULL;
    }

    tmp = strdup(path);
    if (tmp == NULL) {
        message(MESS_ERROR,"strdup of %s failed: %s\n",path,strerror(errno));
        FREE(dirArray[*numDirs-1]);
        FREE(dirArray);
        return NULL;
    }
    
    for (i = *numDirs-2; i >= 0; i--) {
        tmp = dirname(tmp);
        dirArray[i] = strdup(tmp);
        if (dirArray[i] == 0) {
            message(MESS_ERROR,"strdup of %s failed: %s\n",tmp,strerror(errno));
            for(i=i-1;i >= 0; i--)
                FREE(dirArray[i]);
            FREE(dirArray);
            return NULL;
        }
    }

    FREE(tmp);

    return dirArray;
}



static int check_path_sec (struct stat *buf) {
    static uid_t euid;
    euid = geteuid();

    if (!S_ISDIR(buf->st_mode)) {
        message(MESS_DEBUG,"A part of the path is not a directory\n");
        return UNTRUSTED;
    }

    if ((buf->st_uid != euid) && (buf->st_uid != 0)) {
        message(MESS_DEBUG,"Parts of the path do not belong to root or uid %d\n",euid);
        return UNTRUSTED;
    }
    
    if (buf->st_mode & (S_IWGRP | S_IWOTH)) {
        message(MESS_DEBUG,"Parts of the path are group or world writeable\n");
        return UNTRUSTED;
    }

#ifdef WITH_ACL
    /* TODO: Implement ACL checks */
#endif

    return TRUSTED;
}

static int check_path_trust (const char* path) {
    int ret = TRUSTED;
    int numDirs = 0;
    char **dirArray = NULL;
    int i = 0;
    struct stat buf;

    if (path == NULL) {
        message(MESS_ERROR,"Argument error: path is NULL\n");
        return UNTRUSTED;
    }

    if (path[0] != '/') {
        message(MESS_ERROR,"Relative paths are unsupported\n");
        return UNTRUSTED;
    }

    if (linkcount >= MAX_SYMLINKS) {
        message(MESS_ERROR,"Too many symbolic links detected.\n");
        return UNTRUSTED;
    }

        dirArray = fill_dir_array(path,&numDirs);
    if (dirArray == NULL)
        return UNTRUSTED;

    if (numDirs == 0) {
        message(MESS_DEBUG,"No more directories left\n");
        return ret;
    }

    if (numDirs >= MAX_DEPTH) {
        message(MESS_ERROR,"Max path depth reached\n");
        return UNTRUSTED;
    }
    
    for (i = 0; i < numDirs; i++) {
        if (lstat(dirArray[i],&buf) != 0) {
            message(MESS_ERROR, "stat of %s failed: %s\n",dirArray[i],
                    strerror(errno));
            ret =  UNTRUSTED;
            break;
        }

        if (S_ISLNK(buf.st_mode)) {
            char *rl_path;
            rl_path = realpath(dirArray[i],NULL);
            if (rl_path == NULL) {
                    message(MESS_ERROR, "resolving path of %s failed: %s\n",dirArray[i],
                        strerror(errno));
                ret = UNTRUSTED;
                break;
            }

            linkcount++;

            if (check_path_trust(rl_path) == UNTRUSTED) {
                FREE(rl_path);
                ret = UNTRUSTED;
                break;
            }

            linkcount--;

            FREE(rl_path);
            continue;
        }

        if (check_path_sec(&buf) == UNTRUSTED) {
            ret = UNTRUSTED;
            break;
        }

    }

    for (i = 0; i < numDirs; i++)
        FREE(dirArray[i]);
    FREE(dirArray);

    return ret;
}

int is_trusted_dir (const char* path) {
    struct stat buf;
    linkcount = 0;
        errno = 0;

    if (lstat(path,&buf) != 0) {
        message(MESS_ERROR, "stat of %s failed: %s\n",path,strerror(errno));
        if (errno != ENOENT)
            return UNTRUSTED;
    }

    if ( S_ISREG(buf.st_mode) || (errno == ENOENT)) {
        int ret = UNTRUSTED;
        errno = 0;
        char *p = strdup(path);
        if (p == NULL) {
                message(MESS_ERROR,"strdup of %s failed: %s\n",path,strerror(errno));
            return UNTRUSTED;
        }
        ret =  check_path_trust(dirname(p));

        FREE(p);
        return ret;
    }

    return check_path_trust(path);
}
