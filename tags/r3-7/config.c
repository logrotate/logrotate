#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <popt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

#define REALLOC_STEP    10

#if defined(SunOS) && !defined(isblank)
#define isblank(c) 	( (c) == ' ' || (c) == '\t' ) ? 1 : 0
#endif

static char * defTabooExts[] = { ".rpmsave", ".rpmorig", "~", ",v",
				 ".rpmnew", ".swp" };
static int defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char ** tabooExts = NULL;
int tabooCount = 0;

static int readConfigFile(const char * configFile, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);
static int globerr(const char * pathname, int theerr);
static struct rotatePatternElement * parsePattern(const char * pattern,
					const char * configFile, int lineNum);

static int isolateValue(const char * fileName, int lineNum, char * key, 
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

    while (isspace(*chptr)) chptr--;

    *endPtr = chptr + 1;

    return 0;
}

static char *readPath(const char *configFile, int lineNum, char *key,
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

static char * readAddress(const char * configFile, int lineNum, char * key, 
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

static int checkFile(const char * fname) {
    int i;

    /* Check if fname is '.' or '..'; if so, return false */
    if (fname[0] == '.' &&
	(!fname[1] || (fname[1] == '.' && !fname[2])))
	return 0;

    /* Check if fname is ending in a taboo-extension; if so, return
       false */
    for (i = 0; i < tabooCount; i++) {
	if (!strcmp(fname + strlen(fname) - strlen(tabooExts[i]),
	    tabooExts[i])) {
	    message(MESS_ERROR, "Ignoring %s, because of %s "
		    "ending\n", fname, tabooExts[i]);

	    return 0;
	}
    }

      /* All checks have been passed; return true */
    return 1;
}

/* Used by qsort to sort filelist */
static int compar(const void *p, const void *q) {
	return  strcoll(*((char **)p), *((char **)q));
}

/* Free memory blocks pointed to by pointers in namelist and namelist itself */
static void free_namelist (char **namelist, int files_count)
{
    int i;
    for (i=0; i<files_count; ++i)
	free(namelist[i]);
    free(namelist);
}

static struct rotatePatternElement * parsePattern(const char * pattern,
					const char * configFile, int lineNum) {
    struct rotatePatternElement * head, * item;
    struct rotatePatternElement new;
    const char * field;
    const char * start;

    /* dummy head node; we build off of it */
    head = alloca(sizeof(*head));
    item = head;

    start = field = pattern;
    memset(&new, 0, sizeof(new));
    while (*field) {
	if (*field != '%') {
	    field++;
	    continue;
	}

	switch (*field) {
	  case '%':
	    break;
	  case 'f':
	    new.type = RP_FILENAME;
	    break;
	  case 'c':
	    new.type = RP_COUNT;
	    break;
	  default:
	    message(MESS_ERROR, "%s:%d unknown element %c in pattern %s",
		    configFile, lineNum, *field, pattern);
	    return NULL;
	}

	if (new.type != RP_NONE) {
	    if (start != field) {
		/* fixed string */
		item->next = malloc(sizeof(*item->next));
		item->type = RP_STRING;
		item->arg = malloc(field - start + 1);
		strncpy(item->arg, start, field - start);
		item->arg[field-start] = '\0';
		item = item->next;
	    }

	    item->next = malloc(sizeof(*item->next));
	    *item->next = new;
	    item = item->next;

	    memset(&new, 0, sizeof(new));
	}

	field++;
    }

    if (start != field) {
	/* fixed string */
	item->next = malloc(sizeof(*item->next));
	item->type = RP_STRING;
	item->arg = malloc(field - start + 1);
	strncpy(item->arg, start, field-start);
	item->arg[field-start] = '\0';
	item = item->next;
    }
    
    return NULL;
}

int readConfigPath(const char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr) {
    struct stat sb;
    int here;

    if (!tabooExts) {
	tabooExts = malloc(sizeof(*tabooExts) * defTabooCount);
	memcpy(tabooExts, defTabooExts, sizeof(*tabooExts) * defTabooCount);
	tabooCount = defTabooCount;
    }

    if (stat(path, &sb)) {
	message(MESS_ERROR, "cannot stat %s: %s\n", path, strerror(errno));
	return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
	char		**namelist, **p;
	struct dirent	*dp;
	int		files_count,i;
	DIR		*dirp;

	here = open(".", O_RDONLY);
	if (here < 0) {
	    message(MESS_ERROR, "cannot open current directory: %s\n", 
		    strerror(errno));
	    return 1;
	}

	if ( (dirp = opendir(path)) == NULL) {
	    message(MESS_ERROR, "cannot open directory %s: %s\n", path,
		    strerror(errno));
	    return 1;
	}
	files_count = 0;
	namelist = NULL;
	while ((dp = readdir(dirp)) != NULL) {
	    if (checkFile(dp->d_name)) {
		/* Realloc memory for namelist array if necessary */
		if (files_count % REALLOC_STEP == 0) {
		    p = (char **) realloc(namelist, (files_count + REALLOC_STEP) * sizeof(char *));
		    if (p) {
			namelist = p;
			memset(namelist + files_count, '\0', REALLOC_STEP * sizeof(char *));
		    } else {
			free_namelist(namelist, files_count);
			message(MESS_ERROR, "cannot realloc: %s\n", strerror(errno));
			return 1;
		    }
		}
		/* Alloc memory for file name */
		if ( (namelist[files_count] = (char *) malloc( strlen(dp->d_name) + 1)) ) {
		    strcpy(namelist[files_count], dp->d_name);
		    files_count++;
		} else {
		    free_namelist(namelist, files_count);
		    message(MESS_ERROR, "cannot realloc: %s\n", strerror(errno));
		    return 1;
		}
	    }
	}
	closedir( dirp );

	if (files_count > 0) {
	    qsort(namelist, files_count, sizeof(char *), compar);
	} else {
	    return 0;
	}

	if (chdir(path)) {
	    message(MESS_ERROR, "error in chdir(\"%s\"): %s\n", path,
		    strerror(errno));
	    close(here);
	    free_namelist(namelist, files_count);
	    return 1;
	}

	for (i=0; i<files_count; ++i) {
	  assert(namelist[i] != NULL);
	  
	  if (readConfigFile(namelist[i], defConfig, logsPtr, 
			     numLogsPtr)) {
	    fchdir(here);
	    close(here);
	    free_namelist(namelist, files_count);
	    return 1;
	  }
	};

	fchdir(here);
	close(here);
	free_namelist(namelist, files_count);
    } else {
	return readConfigFile(path, defConfig, logsPtr, numLogsPtr);
    }

    return 0;
}

static int globerr(const char * pathname, int theerr) {
    message(MESS_ERROR, "error accessing %s: %s\n", pathname, 
	    strerror(theerr));

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

static int readConfigFile(const char * configFile, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr) {
    int fd;
    char * buf, * endtag;
    char oldchar, foo;
    int length;
    int lineNum = 1;
    int multiplier;
    int i, j, k;
    char * scriptStart = NULL;
    char ** scriptDest = NULL;
    logInfo * newlog = defConfig;
    char * start, * chptr;
    char * dirName;
    struct group * group;
    struct passwd * pw;
    int rc;
    char createOwner[200], createGroup[200];
    int createMode;
    struct stat sb, sb2;
    glob_t globResult;
    const char ** argv;
    int argc, argNum;

    /* FIXME: createOwner and createGroup probably shouldn't be fixed
       length arrays -- of course, if we aren't run setuid it doesn't
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

    /* knowing the buffer ends with a newline makes things (a bit) cleaner */
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
	    } else if (!strcmp(start, "delaycompress")) {
		newlog->flags |= LOG_FLAG_DELAYCOMPRESS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nodelaycompress")) {
		newlog->flags &= ~LOG_FLAG_DELAYCOMPRESS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "sharedscripts")) {
		newlog->flags |= LOG_FLAG_SHAREDSCRIPTS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nosharedscripts")) {
		newlog->flags &= ~LOG_FLAG_SHAREDSCRIPTS;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "copytruncate")) {
		newlog->flags |= LOG_FLAG_COPYTRUNCATE;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nocopytruncate")) {
		newlog->flags &= ~LOG_FLAG_COPYTRUNCATE;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "copy")) {
		newlog->flags |= LOG_FLAG_COPY;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nocopy")) {
		newlog->flags &= ~LOG_FLAG_COPY;

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
	    } else if (!strcmp(start, "mailfirst")) {
		newlog->flags |= LOG_FLAG_MAILFIRST;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "maillast")) {
		newlog->flags &= ~LOG_FLAG_MAILFIRST;

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
			    "create\n", configFile, lineNum);
		    return 1;
		}

		if (rc > 0)
		    newlog->createMode = createMode;
		
		if (rc > 1) {
		    pw = getpwnam(createOwner);
		    if (!pw) {
			message(MESS_ERROR, "%s:%d unknown user '%s'\n", 
				configFile, lineNum, createOwner);
			return 1;
		    } 
		    newlog->createUid = pw->pw_uid;
		    endpwent();
		} 
		if (rc > 2) {
		    group = getgrnam(createGroup);
		    if (!group) {
			message(MESS_ERROR, "%s:%d unknown group '%s'\n", 
				configFile, lineNum, createGroup);
			return 1;
		    } 
		    newlog->createGid = group->gr_gid;
		    endgrent();
		} 

		newlog->flags |= LOG_FLAG_CREATE;

		*endtag = oldchar, start = endtag;
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
		    } else if (start[length] == 'G') {
			start[length] = '\0';
			multiplier = 1024 * 1024 * 1024;
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
		    if (*chptr || newlog->rotateCount < 0) {
			message(MESS_ERROR, "%s:%d bad rotation count '%s'\n",
				    configFile, lineNum, start);
			return 1;
		    }
		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "start")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "start count", &start,
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->logStart = strtoul(start, &chptr, 0);
		    if (*chptr || newlog->logStart < 0) {
		      message(MESS_ERROR, "%s:%d bad start count '%s'\n",
			      configFile, lineNum, start);
		      return 1;
		    }
		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "errors")) {
		message(MESS_DEBUG, "%s: %d: the errors directive is deprecated and no longer used.\n",
			configFile, lineNum);
	    } else if (!strcmp(start, "mail")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->logAddress = readAddress(configFile, lineNum, 
							"mail", &start))) {
		    return 1;
		}
	    } else if (!strcmp(start, "nomail")) {
		/* hmmm, we could lose memory here, but not much */
		newlog->logAddress = NULL;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "missingok")) {
		newlog->flags |= LOG_FLAG_MISSINGOK;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nomissingok")) {
		newlog->flags &= ~LOG_FLAG_MISSINGOK;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "prerotate")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->pre;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "firstaction")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->first;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "postrotate")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->post;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "lastaction")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->last;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "tabooext")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR, "%s:%d tabooext may not appear inside "
			    "of log file definition\n", configFile, lineNum);
		    return 1;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "tabooext", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    if (*start == '+') {
			start++;
			while (isspace(*start) && *start) start++;
		    } else {
			free(tabooExts);
			tabooCount = 0;
			tabooExts = malloc(1);
		    }

		    while (*start) {
			chptr = start;
			while (!isspace(*chptr) && *chptr != ',' && *chptr)
			    chptr++;

			tabooExts = realloc(tabooExts, sizeof(*tabooExts) * 
						(tabooCount + 1));
			/* this is a memory leak if the list gets reset */
			tabooExts[tabooCount] = malloc(chptr - start + 1);
			strncpy(tabooExts[tabooCount], start, chptr - start);
			tabooExts[tabooCount][chptr - start] = '\0';
			tabooCount++;

			start = chptr;
			if (*start == ',') start++;
			while (isspace(*start) && *start) start++;
		    }

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "include")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR, "%s:%d include may not appear inside "
			    "of log file definition\n", configFile, lineNum);
		    return 1;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "include", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    message(MESS_DEBUG, "including %s\n", start);

		    if (readConfigPath(start, defConfig, logsPtr, 
				       numLogsPtr))
			return 1;

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "pattern")) {
		char * patternString;

		*endtag = oldchar, start = endtag;
		if (!(patternString = readPath(configFile, lineNum,
						"pattern", &start))) {
		    return 1;
		}

		newlog->rotatePattern = parsePattern(patternString,
					    configFile, lineNum);
		if (!newlog->rotatePattern) return 1;

		message(MESS_DEBUG, "pattern is now %s\n", patternString);
	    } else if (!strcmp(start, "olddir")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->oldDir = readPath(configFile, lineNum,
						"olddir", &start))) {
		    return 1;
		}

#if 0
		if (stat(newlog->oldDir, &sb)) {
		    message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum, 
				newlog->oldDir, strerror(errno));
		    return 1;
		}

		if (!S_ISDIR(sb.st_mode)) {
		    message(MESS_ERROR, "%s:%d olddir path %s is not a "
				"directory\n", configFile, lineNum, 
				newlog->oldDir);
		    return 1;
		}
#endif

		message(MESS_DEBUG, "olddir is now %s\n", newlog->oldDir);
	    } else if (!strcmp(start, "extension")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "extension name", &start, 
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->extension = strdup(start);

		    *endtag = oldchar, start = endtag;
		}

		message(MESS_DEBUG, "extension is now %s\n", newlog->extension);

	    } else if (!strcmp(start, "compresscmd")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->compress_prog = readPath(configFile, lineNum, "compress", &start))) {
		    return 1;
		}

		if (access(newlog->compress_prog, X_OK)) {
		    message(MESS_ERROR, "%s:%d compression program %s is not an executable file\n", configFile, lineNum, 
				newlog->compress_prog);
		    return 1;
		}

		message(MESS_DEBUG, "compress_prog is now %s\n", newlog->compress_prog);

	    } else if (!strcmp(start, "uncompresscmd")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->uncompress_prog = readPath(configFile, lineNum, "uncompress", &start))) {
		    return 1;
		}

		if (access(newlog->uncompress_prog, X_OK)) {
		    message(MESS_ERROR, "%s:%d uncompression program %s is not an executable file\n", configFile, lineNum, 
				newlog->uncompress_prog);
		    return 1;
		}

		message(MESS_DEBUG, "uncompress_prog is now %s\n", newlog->uncompress_prog);

	    } else if (!strcmp(start, "compressoptions")) {
		char * options;

		*endtag = oldchar, start = endtag;
		if (!(options = readPath(configFile, lineNum, "compressoptions", &start))) {
		    return 1;
		}

		if (poptParseArgvString(options, 
					&newlog->compress_options_count,
					&newlog->compress_options_list)) {
		    message(MESS_ERROR, "%s:%d invalid compression options\n", 
			    configFile, lineNum);
		    return 1;
		}

		message(MESS_DEBUG, "compress_options is now %s\n", options);
	    } else if (!strcmp(start, "compressext")) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->compress_ext = readPath(configFile, lineNum, "compress-ext", &start))) {
		    return 1;
		}

		message(MESS_DEBUG, "compress_ext is now %s\n", newlog->compress_ext);
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
	} else if (*start == '/' || *start == '"' || *start == '\'') {
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
	    while (*endtag != '{' && *endtag != '\0') endtag++;
	    if (*endtag != '{') {
		message(MESS_ERROR, "%s:%d missing end of line\n",
			configFile, lineNum);
	    }
	    *endtag = '\0';

	    if (poptParseArgvString(start, &argc, &argv)) {
		message(MESS_ERROR, "%s:%d error parsing filename\n",
			configFile, lineNum);
		return 1;
	    } else if (argc < 1) {
		message(MESS_ERROR, "%s:%d { expected after log file name(s)\n",
			configFile, lineNum);
		return 1;
	    }

	    /* XXX this leaks the result of the glob <shrug> */
	    newlog->files = NULL;
	    newlog->numFiles = 0;
	    for (argNum = 0; argNum < argc; argNum++) {
		rc = glob(argv[argNum], GLOB_NOCHECK, globerr, &globResult);
		if (rc == GLOB_ABORTED) {
		    if(newlog->flags & LOG_FLAG_MISSINGOK)
		        continue;

		    message(MESS_ERROR, "%s:%d glob failed for %s\n",
			    configFile, lineNum, argv[argNum]);
		    return 1;
		}

		newlog->files = realloc(newlog->files, sizeof(*newlog->files) * 
				   (newlog->numFiles + globResult.gl_pathc));

		for (i = 0; i < globResult.gl_pathc; i++) {
		    /* if we glob directories we can get false matches */
		    if (!lstat(globResult.gl_pathv[i], &sb) && 
				    S_ISDIR(sb.st_mode)) 
			continue;

		    for (j = 0; j < *numLogsPtr - 1; j++) {
			for (k = 0; k < (*logsPtr)[j].numFiles; k++) {
			    if (!strcmp((*logsPtr)[j].files[k], 
					globResult.gl_pathv[i])) {
				message(MESS_ERROR, 
					"%s:%d duplicate log entry for %s\n",
					configFile, lineNum, 
					globResult.gl_pathv[i]);
				return 1;
			    }
			}
		    }

		    newlog->files[newlog->numFiles] = 
			    globResult.gl_pathv[i];
		    newlog->numFiles++;
		}
	    }

	    newlog->pattern = strdup(start);

	    message(MESS_DEBUG, "reading config info for %s\n", start);

	    free(argv);

	    start = endtag + 1;
	} else if (*start == '}') {
	    if (newlog == defConfig) {
		message(MESS_ERROR, "%s:%d unxpected }\n", configFile, lineNum);
		return 1;
	    }

	    if (newlog->oldDir) {
		for (i = 0; i < newlog->numFiles; i++) {
		    char *ld;
		    dirName = ourDirName(newlog->files[i]);
		    if (stat(dirName, &sb2)) {
			message(MESS_ERROR, "%s:%d error verifying log file "
				    "path %s: %s\n", configFile, lineNum, 
				    dirName, strerror(errno));
			free(dirName);
			return 1;
		    }
		    ld = alloca(strlen(dirName) + strlen(newlog->oldDir) + 2);
		    sprintf(ld, "%s/%s", dirName, newlog->oldDir);
		    free(dirName);

		    if(newlog->oldDir[0] != '/') dirName = ld;
		    else dirName = newlog->oldDir;
		    if(stat(dirName, &sb)) {
			message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum,
				dirName, strerror(errno));
				return 1;
		    }

		    if (sb.st_dev != sb2.st_dev) {
			message(MESS_ERROR, "%s:%d olddir %s and log file %s "
				    "are on different devices\n", configFile,
				    lineNum, newlog->oldDir, newlog->files[i]);
			return 1;
		    }
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
			"or a filename (possibly in double quotes)\n", 
			configFile, lineNum);

	    while (*start != '\n') start++;
	    lineNum++;
	    start++;
	}
    }

    if(scriptStart) {
      message(MESS_ERROR, "%s:prerotate or postrotate without endscript\n",
	      configFile);
      return 1;
    }

    return 0;
}

