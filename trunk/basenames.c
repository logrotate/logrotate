#include <stdlib.h>
#include <string.h>

#include "basenames.h"

/* Return NAME with any leading path stripped off.  */

char *ourBaseName(char *name) {
    char *base;

    base = strrchr(name, '/');
    return base ? base + 1 : name;
}

static void stripTrailingSlashes(char *path) {
    char * last;

    last = path + strlen(path) - 1;
    while (last > path && *last == '/')
	*last-- = '\0';
}

char * ourDirName(char * origname) {
    char * slash;
    char * name;
    int i = strlen(origname);

    name = strcpy(malloc(i + 1), origname);

    stripTrailingSlashes(name);

    slash = strrchr(name, '/');

    if (!slash)
	return (char *) ".";
    else {
	/* Remove any trailing slashes and final element. */
	while (slash > name && *slash == '/')
	    --slash;
	slash[1] = '\0';
	return name;
    }
}
