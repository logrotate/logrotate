#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

static char * tabooExts[] = { ".rpmsave", ".rpmorig", "~" };
static int tabooCount = sizeof(tabooExts) / sizeof(char *);

static int readConfigFile(char * configFile, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);

static int isolateValue(char * fileName, int lineNum, char * key, 
			char ** startPtr, char ** endPtr) {
    char * chptr = *startPtr;

    while (isblank(*chptr)) chptr++;
    if (*chptr == '=') {
	chptr++;
	while (*chptr && isblank(*chptr)) chptr++;
    }

    if (*chptr == '\n') {
	message(MESS_ERROR, "%s:%d argument expected after %s\n", 
		fileName, lineNum, key);
	return 1;
    }

    *startPtr = chptr;

    while (*chptr != '\n') chptr++;
	
    while (isblank(*chptr)) chptr--;

    if (*chptr == '\n')
	*endPtr = chptr;
    else
	*endPtr = chptr + 1;

    return 0;
}

static char *readPath(char *configFile, int lineNum, char *key,
		      char **startPtr) {
    char oldchar;
    char *endtag, *chptr;
    char *start = *startPtr;
    char *path;

    if (!isolateValue(configFile, lineNum, key, &start,
		      &endtag)) {
	oldchar = *endtag, *endtag = '\0';

	chptr = start;

	/* this is technically too restrictive -- let's see if anyone
	   complains */
	while (*chptr && isprint(*chptr) && *chptr != ' ')
	    chptr++;
	if (*chptr) {
	    message(MESS_ERROR, "%s:%d bad %s path %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}
	path = strdup(start);

	*endtag = oldchar, start = endtag;

	*startPtr = start;

	return path;
    } else
	return NULL;
}

static char * readAddress(char * configFile, int lineNum, char * key, 
			  char ** startPtr) {
    char oldchar;
    char * endtag, * chptr;
    char * start = *startPtr;
    char * address;

    if (!isolateValue(configFile, lineNum, key, &start, 
		      &endtag)) {
	oldchar = *endtag, *endtag = '\0';

	chptr = start;
	while (*chptr && isprint(*chptr) && *chptr != ' ') 
	    chptr++;
	if (*chptr) {
	    message(MESS_ERROR, "%s:%d bad %s address %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}

	address = strdup(start);

	*endtag = oldchar, start = endtag;

	*startPtr = start;

	return address;
    } else
	return NULL;
}

int readConfigPath(char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr) {
    struct stat sb;
    DIR * dir;
    struct dirent * ent;
    int here;
    int i;

    if (stat(path, &sb)) {
	message(MESS_ERROR, "cannot stat %s: %s\n", path, strerror(errno));
	return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
	dir = opendir(path);
	if (!dir) {
	    message(MESS_ERROR, "failed to open directory %s: %s\n", path,
			strerror(errno));
	    return 1;
	}

	here = open(".", O_RDONLY);
	if (here < 0) {
	    message(MESS_ERROR, "cannot open current directory: %s\n", 
		    strerror(errno));
	    closedir(dir);
	    return 1;
	}

	if (chdir(path)) {
	    message(MESS_ERROR, "error in chdir(\"%s\"): %s\n", path,
		    strerror(errno));
	    close(here);
	    closedir(dir);
	    return 1;
	}

	do {
	    errno = 0;
	    ent = readdir(dir);
	    if (errno) {
		message(MESS_ERROR, "readdir() from %s failed: %s\n", path,
			strerror(errno));
		fchdir(here);
		close(here);
		closedir(dir);
		return 1;
	    } else if (ent && ent->d_name[0] == '.' && (!ent->d_name[1] || 
		(ent->d_name[1] == '.' && !ent->d_name[2]))) {
		/* noop */
	    } else if (ent) {
		for (i = 0; i < tabooCount; i++) {
		    if (!strcmp(ent->d_name + strlen(ent->d_name) -
				strlen(tabooExts[i]), tabooExts[i])) {
			message(MESS_DEBUG, "Ignoring %s, because of %s "
				"ending\n", ent->d_name, tabooExts[i]);
			break;
		    }
		}

		if (i == tabooCount) {
		    if (readConfigFile(ent->d_name, defConfig, logsPtr, 
				       numLogsPtr)) {
			fchdir(here);
			close(here);
			return 1;
		    }
		}
	    }
	} while (ent);

	closedir(dir);

	fchdir(here);
	close(here);
    } else {
	return readConfigFile(path, defConfig, logsPtr, numLogsPtr);
    }

    return 0;
}

static int readConfigFile(char * configFile, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr) {
    int fd;
    char * buf, * endtag;
    char oldchar, foo;
    int length;
    int lineNum = 1;
    int multiplier;
    int i;
    char * scriptStart = NULL;
    char ** scriptDest = NULL;
    logInfo * newlog = defConfig;
    char * start, * chptr;
    char * dirName;
    struct group * group;
    struct passwd * pw;
    int rc;
    char createOwner[200], createGroup[200];
    mode_t createMode;
    struct stat sb, sb2;

    /* FIXME: createOWnder and createGroup probably shouldn't be fixed
       length arrays -- of course, if we aren't run setuid in doesn't
       matter much */

    fd = open(configFile, O_RDONLY);
    if (fd < 0) {
	message(MESS_ERROR, "failed to open config file %s: %s\n",
		configFile, strerror(errno));
	return 1;
    }

    if (fstat(fd, &sb)) {
	message(MESS_ERROR, "fstat of %s failed: %s\n", configFile,
		strerror(errno));
	close(fd);
	return 1;
    }
    if (!S_ISREG(sb.st_mode)) {
	message(MESS_DEBUG, "Ignoring %s because it's not a regular file.\n",
		configFile);
	close(fd);
	return 0;
    }

    length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    buf = alloca(length + 2);
    if (!buf) {
	message(MESS_ERROR, "alloca() of %d bytes failed\n", length);
	close(fd);
	return 1;
    }

    if (read(fd, buf, length) != length) {
	message(MESS_ERROR, "failed to read %s: %s\n", configFile, 
		strerror(errno));
	close(fd);
	return 1;
    }

    close(fd);

    /* knowing the buffer ends with a newline makes things a (bit) cleaner */
    buf[length + 1] = '\0';
    buf[length] = '\n';

    message(MESS_DEBUG, "reading config file %s\n", configFile);

    start = buf;
    while (*start) {
	while (isblank(*start) && (*start)) start++;
	if (*start == '#') {
	    while (*start != '\n') start++;
	}

	if (*start == '\n') {
	    start++;
	    lineNum++;
	    continue;
	}

	if (scriptStart) {
	    if (!strncmp(start, "endscript", 9)) {
		chptr = start + 9;
		while (isblank(*chptr)) chptr++;
		if (*chptr == '\n') {
		    endtag = start;
		    while (*endtag != '\n') endtag--;
		    endtag++;
		    *scriptDest = malloc(endtag - scriptStart + 1);
		    strncpy(*scriptDest, scriptStart, endtag - scriptStart);
		    (*scriptDest)[endtag - scriptStart] = '\0';
		    start = chptr + 1;
		    lineNum++;

		    scriptDest = NULL;
		    scriptStart = NULL;
		}
	    } 

	    if (scriptStart) {
		while (*start != '\n') start++;
		lineNum++;
		start++;
	    }
	} else if (isalpha(*start)) {
	    endtag = start;
	    while (isalpha(*endtag)) endtag++;
	    oldchar = *endtag;
	    *endtag = '\0';

	    if (!strcmp(start, "compress")) {
		newlog->flags |= LOG_FLAG_COMPRESS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nocompress")) {
		newlog->flags &= ~LOG_FLAG_COMPRESS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "ifempty")) {
		newlog->flags |= LOG_FLAG_IFEMPTY;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "notifempty")) {
		newlog->flags &= ~LOG_FLAG_IFEMPTY;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "noolddir")) {
		newlog->oldDir = NULL;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "create")) {
		*endtag = oldchar, start = endtag;

		endtag = start;
		while (*endtag != '\n') endtag++;
		while (isspace(*endtag)) endtag--;
		endtag++;
		oldchar = *endtag, *endtag = '\0';
		
		rc = sscanf(start, "%o %s %s%c", &createMode, 
				createOwner, createGroup, &foo);
		if (rc == 4) {
		    message(MESS_ERROR, "%s:%d extra arguments for "
			    "create\n", configFile, lineNum, start[length]);
		    return 1;
		}

		if (rc > 0)
		    newlog->createMode = createMode;
		
		if (rc > 1) {
		    pw = getpwnam(createOwner);
		    if (!pw) {
			message(MESS_ERROR, "%s:%d unkown user '%s'\n", 
				configFile, lineNum, createOwner);
			return 1;
		    } 
		    newlog->createUid = pw->pw_uid;
		    endpwent();
		} 
		if (rc > 2) {
		    group = getgrnam(createGroup);
		    if (!group) {
			message(MESS_ERROR, "%s:%d unkown group '%s'\n", 
				configFile, lineNum, createGroup);
			return 1;
		    } 
		    newlog->createGid = group->gr_gid;
		    endgrent();
		} 

		newlog->flags |= LOG_FLAG_CREATE;

		*endtag = oldchar, start = endtag;
		while (*start != '\n') start++;
	    } else if (!strcmp(start, "nocreate")) {
		newlog->flags &= ~LOG_FLAG_CREATE;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "size")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "size", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    length = strlen(start) - 1;
		    if (start[length] == 'k') {
			start[length] = '\0';
			multiplier = 1024;
		    } else if (start[length] == 'M') {
			start[length] = '\0';
			multiplier = 1024 * 1024;
		    } else if (!isdigit(start[length])) {
			message(MESS_ERROR, "%s:%d unknown unit '%c'\n",
				    configFile, lineNum, start[length]);
			return 1;
		    } else {
			multiplier = 1;
		    }

		    newlog->threshhold = multiplier * strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MESS_ERROR, "%s:%d bad size '%s'\n",
				    configFile, lineNum, start);
			return 1;
		    }

		    newlog->criterium = ROT_SIZE;

		    *endtag = oldchar, start = endtag;
		}
#if 0   /* this seems like such a good idea :-( */
	    } else if (!strcmp(start, "days")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "size", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->threshhold = strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MESS_ERROR, "%s:%d bad number of days'%s'\n",
				    configFile, lineNum, start);
			return 1;
		    }

		    newlog->criterium = ROT_DAYS;

		    *endtag = oldchar, start = endtag;
		}
#endif
	    } else if (!strcmp(start, "daily")) {
		*endtag = oldchar, start = endtag;

		newlog->criterium = ROT_DAYS;
		newlog->threshhold = 1;
	    } else if (!strcmp(start, "monthly")) {
		*endtag = oldchar, start = endtag;

		newlog->criterium = ROT_MONTHLY;
	    } else if (!strcmp(start, "weekly")) {
		*endtag = oldchar, start = endtag;

		newlog->criterium = ROT_WEEKLY;
	    } else if (!strcmp(start, "rotate")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "rotate count", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->rotateCount = strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MESS_ERROR, "%s:%d bad rotation count'%s'\n",
				    configFile, lineNum, start);
			return 1;
		    }

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "errors")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->errAddress = readAddress(configFile, lineNum, 
						       "error", &start))) {
		    return 1;
		}
	    } else if (!strcmp(start, "mail")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->logAddress = readAddress(configFile, lineNum, 
						        "mail", &start))) {
		    return 1;
		}
	    } else if (!strcmp(start, "prerotate")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->pre;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "postrotate")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->post;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "include")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR, "%s:%d include may not appear inside "
			    "of log file definition", configFile, lineNum);
		    return 1;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "size", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    message(MESS_DEBUG, "including %s\n", start);

		    if (readConfigPath(start, defConfig, logsPtr, 
				       numLogsPtr))
			return 1;

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "olddir")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->oldDir = readPath(configFile, lineNum,
						"olddir", &start))) {
		    return 1;
		}

		if (stat(newlog->oldDir, &sb)) {
		    message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum, 
				newlog->oldDir, strerror(errno));
		    return 1;
		}

		if (!S_ISDIR(sb.st_mode)) {
		    message(MESS_ERROR, "%s:%d olddir path %s is not a "
				"directory %s: %s\n", configFile, lineNum, 
				newlog->oldDir, strerror(errno));
		    return 1;
		}

		message(MESS_DEBUG, "olddir is now %s\n", newlog->oldDir);
	    } else {
		message(MESS_ERROR, "%s:%d unknown option '%s' "
			    "-- ignoring line\n", configFile, lineNum, start);

		*endtag = oldchar, start = endtag;
	    }

	    while (isblank(*start)) start++;

	    if (*start != '\n') {
		message(MESS_ERROR, "%s:%d unexpected text\n", configFile,
			    lineNum);
		while (*start != '\n') start++;
	    }

	    lineNum++;
	    start++;
	} else if (*start == '/') {
	    if (newlog != defConfig) {
		message(MESS_ERROR, "%s:%d unexpected log filename\n", 
			configFile, lineNum);
		return 1;
	    }

	    (*numLogsPtr)++;
	    *logsPtr = realloc(*logsPtr, sizeof(**logsPtr) * *numLogsPtr);
	    newlog = *logsPtr + *numLogsPtr - 1;
	    memcpy(newlog, defConfig, sizeof(*newlog));
	    
	    endtag = start;
	    while (!isspace(*endtag)) endtag++;
	    oldchar = *endtag;
	    *endtag = '\0';

	    for (i = 0; i < *numLogsPtr - 1; i++) {
		if (!strcmp((*logsPtr)[i].fn, start)) {
		    message(MESS_ERROR, "%s:%d duplicate log entry for %s\n",
			    configFile, lineNum, start);
		    return 1;
		}
	    }

	    newlog->fn = strdup(start);

	    message(MESS_DEBUG, "reading config info for %s\n", start);

	    *endtag = oldchar, start = endtag;

	    while (*start && isspace(*start) && *start != '{') {
		if (*start == '\n') lineNum++;
		start++;
	    }

	    if (*start != '{') {
		message(MESS_ERROR, "%s:%d { expected after log file name\n",
			configFile, lineNum);
		return 1;
	    }

	    start++;
	    while (isblank(*start) && *start != '\n') start++;
	
	    if (*start != '\n') {
		message(MESS_ERROR, "%s:%d unexpected text after {\n");
		return 1;
	    }
	} else if (*start == '}') {
	    if (newlog == defConfig) {
		message(MESS_ERROR, "%s:%d unxpected }\n", configFile, lineNum);
		return 1;
	    }

	    if (newlog->oldDir) {
		if (stat(newlog->oldDir, &sb)) {
		    message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum, 
				newlog->oldDir, strerror(errno));
		    return 1;
		}

		dirName = ourDirName(newlog->fn);
		if (stat(dirName, &sb2)) {
		    message(MESS_ERROR, "%s:%d error verifying log file "
				"path %s: %s\n", configFile, lineNum, 
				dirName, strerror(errno));
		    free(dirName);
		    return 1;
		}

		free(dirName);

		if (sb.st_dev != sb2.st_dev) {
		    message(MESS_ERROR, "%s:%d olddir %s and log file %s "
				"are on different devices\n", configFile,
				lineNum, newlog->oldDir, newlog->fn);
		    return 1;
		}
	    }

	    newlog = defConfig;

	    start++;
	    while (isblank(*start)) start++;

	    if (*start != '\n') {
		message(MESS_ERROR, "%s:%d, unexpected text after {\n",
			configFile, lineNum);
	    }
	} else {
	    message(MESS_ERROR, "%s:%d lines must begin with a keyword "
			"or a /\n", configFile, lineNum);

	    while (*start != '\n') start++;
	    lineNum++;
	    start++;
	}
    }

    return 0;
}

