#ifndef TRUSTDIR_H
#define TRUSTDIR_H

enum { 
    MAX_SYMLINKS = 5,
    MAX_DEPTH = 50 /* maximal directories to traverse */
};

enum trust {
    UNTRUSTED = 0,
    TRUSTED = 1
};


int is_trusted_dir(const char* path);

#endif 
