#include <stdlib.h>
#include <string.h>

#include "basenames.h"

/* Return NAME with any leading path stripped off.  */

char *ourBaseName(char *name)
{
    char *base;

    base = strrchr(name, '/');
    return base ? base + 1 : name;
}

static void stripTrailingSlashes(char *path)
{
    char *last;

    last = path + strlen(path) - 1;
    while (last > path && *last == '/')
	*last-- = '\0';
}

char *ourDirName(char *origname)
{
    char *slash;
    char *name;

    name = strdup(origname);

    stripTrailingSlashes(name);

    slash = strrchr(name, '/');

    if (!slash) {
	/* No slash, must be current directory */
	free(name);
	/* strdup used, as the return value will be free()ed at some point */
	return strdup(".");
    } else {
	/* Remove any trailing slashes and final element. */
	while (slash > name && *slash == '/')
	    --slash;
	slash[1] = '\0';
	return name;
    }
}
