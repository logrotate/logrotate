#include <sys/queue.h>
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
#include <wchar.h>
#include <wctype.h>
#include <fnmatch.h>

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

static char *defTabooExts[] = { ".rpmsave", ".rpmorig", "~", ",v",
    ".rpmnew", ".swp", ".cfsaved", ".rhn-cfg-tmp-*"
};
static int defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char **tabooExts = NULL;
int tabooCount = 0;

static int readConfigFile(const char *configFile, struct logInfo *defConfig);
static int globerr(const char *pathname, int theerr);

static int isolateValue(const char *fileName, int lineNum, char *key,
			char **startPtr, char **endPtr)
{
    char *chptr = *startPtr;

    while (isblank(*chptr))
	chptr++;
    if (*chptr == '=') {
	chptr++;
	while (*chptr && isblank(*chptr))
	    chptr++;
    }

    if (*chptr == '\n') {
	message(MESS_ERROR, "%s:%d argument expected after %s\n",
		fileName, lineNum, key);
	return 1;
    }

    *startPtr = chptr;

    while (*chptr != '\n')
	chptr++;

    while (isspace(*chptr))
	chptr--;

    *endPtr = chptr + 1;

    return 0;
}

static char *readPath(const char *configFile, int lineNum, char *key,
		      char **startPtr)
{
    char oldchar;
    char *endtag, *chptr;
    char *start = *startPtr;
    char *path;

    wchar_t pwc;
    size_t len;

    if (!isolateValue(configFile, lineNum, key, &start, &endtag)) {
	oldchar = *endtag, *endtag = '\0';

	chptr = start;

	while( (len = mbrtowc(&pwc, chptr, strlen(chptr), NULL)) != 0 ) {
		if( len == (size_t)(-1) || len == (size_t)(-2) || !iswprint(pwc) || iswblank(pwc) ) {
		    message(MESS_ERROR, "%s:%d bad %s path %s\n",
			    configFile, lineNum, key, start);
		    return NULL;
		}
		chptr += len;
	}

/*
	while (*chptr && isprint(*chptr) && *chptr != ' ')
	    chptr++;
	if (*chptr) {
	    message(MESS_ERROR, "%s:%d bad %s path %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}
*/

	path = strdup(start);


	*endtag = oldchar, start = endtag;

	*startPtr = start;

	return path;
    } else
	return NULL;
}

static char *readAddress(const char *configFile, int lineNum, char *key,
			 char **startPtr)
{
    char oldchar;
    char *endtag, *chptr;
    char *start = *startPtr;
    char *address;

    if (!isolateValue(configFile, lineNum, key, &start, &endtag)) {
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

static int checkFile(const char *fname)
{
	int i;
	char pattern[PATH_MAX];

	/* Check if fname is '.' or '..'; if so, return false */
	if (fname[0] == '.' && (!fname[1] || (fname[1] == '.' && !fname[2])))
		return 0;

	/* Check if fname is ending in a taboo-extension; if so, return false */
	for (i = 0; i < tabooCount; i++) {
		snprintf(pattern, sizeof(pattern), "*%s", tabooExts[i]);
		if (!fnmatch(pattern, fname, 0))
		{
			message(MESS_DEBUG, "Ignoring %s, because of %s ending\n",
					fname, tabooExts[i]);
			return 0;
		}
	}

	/* All checks have been passed; return true */
	return 1;
}

/* Used by qsort to sort filelist */
static int compar(const void *p, const void *q)
{
    return strcoll(*((char **) p), *((char **) q));
}

/* Free memory blocks pointed to by pointers in a 2d array and the array itself */
static void free_2d_array(char **array, int lines_count)
{
    int i;
    for (i = 0; i < lines_count; ++i)
	free(array[i]);
    free(array);
}

static void copyLogInfo(struct logInfo *to, struct logInfo *from)
{
    memset(to, 0, sizeof(*to));
    if (from->oldDir)
	to->oldDir = strdup(from->oldDir);
    to->criterium = from->criterium;
    to->threshhold = from->threshhold;
    to->minsize = from->minsize;
    to->rotateCount = from->rotateCount;
    to->rotateAge = from->rotateAge;
    to->logStart = from->logStart;
    if (from->pre)
	to->pre = strdup(from->pre);
    if (from->post)
	to->post = strdup(from->post);
    if (from->first)
	to->first = strdup(from->first);
    if (from->last)
	to->last = strdup(from->last);
    if (from->logAddress)
	to->logAddress = strdup(from->logAddress);
    if (from->extension)
	to->extension = strdup(from->extension);
    if (from->compress_prog)
	to->compress_prog = strdup(from->compress_prog);
    if (from->uncompress_prog)
	to->uncompress_prog = strdup(from->uncompress_prog);
    if (from->compress_ext)
	to->compress_ext = strdup(from->compress_ext);
    to->flags = from->flags;
    to->createMode = from->createMode;
    to->createUid = from->createUid;
    to->createGid = from->createGid;
    if (from->compress_options_count) {
        poptDupArgv(from->compress_options_count, from->compress_options_list, 
                    &to->compress_options_count,  &to->compress_options_list);
    }
	if (from->dateformat)
		to->dateformat = strdup(from->dateformat);
}

static void freeLogInfo(struct logInfo *log)
{
    if (log->pattern)
	free(log->pattern);
    if (log->files)
	free_2d_array(log->files, log->numFiles);
    if (log->oldDir)
	free(log->oldDir);
    if (log->pre)
	free(log->pre);
    if (log->post)
	free(log->post);
    if (log->first)
	free(log->first);
    if (log->last)
	free(log->last);
    if (log->logAddress)
	free(log->logAddress);
    if (log->extension)
	free(log->extension);
    if (log->compress_prog)
	free(log->compress_prog);
    if (log->uncompress_prog)
	free(log->uncompress_prog);
    if (log->compress_ext)
	free(log->compress_ext);
    if (log->compress_options_list)
	free(log->compress_options_list);
	free(log->dateformat);
}

static struct logInfo *newLogInfo(struct logInfo *template)
{
	struct logInfo *new;

	if ((new = malloc(sizeof(*new))) == NULL)
		return NULL;

	copyLogInfo(new, template);
	TAILQ_INSERT_TAIL(&logs, new, list);
	numLogs++;

	return new;
}

static void removeLogInfo(struct logInfo *log)
{
	if (log == NULL)
		return;

	freeLogInfo(log);
	TAILQ_REMOVE(&logs, log, list);
	numLogs--;
}

static void freeTailLogs(int num)
{
	message(MESS_DEBUG, "removing last %d log configs\n", num);

	while (num--)
		removeLogInfo(*(logs.tqh_last));
}

static int readConfigPath(const char *path, struct logInfo *defConfig)
{
    struct stat sb;
    int here, oldnumlogs, result = 1;
	struct logInfo defConfigBackup;

    if (stat(path, &sb)) {
	message(MESS_ERROR, "cannot stat %s: %s\n", path, strerror(errno));
	return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
	char **namelist, **p;
	struct dirent *dp;
	int files_count, i;
	DIR *dirp;

	here = open(".", O_RDONLY);
	if (here < 0) {
	    message(MESS_ERROR, "cannot open current directory: %s\n",
		    strerror(errno));
	    return 1;
	}

	if ((dirp = opendir(path)) == NULL) {
	    message(MESS_ERROR, "cannot open directory %s: %s\n", path,
		    strerror(errno));
	    close(here);
	    return 1;
	}
	files_count = 0;
	namelist = NULL;
	while ((dp = readdir(dirp)) != NULL) {
	    if (checkFile(dp->d_name)) {
		/* Realloc memory for namelist array if necessary */
		if (files_count % REALLOC_STEP == 0) {
		    p = (char **) realloc(namelist,
					  (files_count +
					   REALLOC_STEP) * sizeof(char *));
		    if (p) {
			namelist = p;
			memset(namelist + files_count, '\0',
			       REALLOC_STEP * sizeof(char *));
		    } else {
			free_2d_array(namelist, files_count);
			closedir(dirp);
			close(here);
			message(MESS_ERROR, "cannot realloc: %s\n",
				strerror(errno));
			return 1;
		    }
		}
		/* Alloc memory for file name */
		if ((namelist[files_count] =
		     (char *) malloc(strlen(dp->d_name) + 1))) {
		    strcpy(namelist[files_count], dp->d_name);
		    files_count++;
		} else {
		    free_2d_array(namelist, files_count);
		    closedir(dirp);
		    close(here);
		    message(MESS_ERROR, "cannot realloc: %s\n",
			    strerror(errno));
		    return 1;
		}
	    }
	}
	closedir(dirp);

	if (files_count > 0) {
	    qsort(namelist, files_count, sizeof(char *), compar);
	} else {
	    close(here);
	    return 0;
	}

	if (chdir(path)) {
	    message(MESS_ERROR, "error in chdir(\"%s\"): %s\n", path,
		    strerror(errno));
	    close(here);
	    free_2d_array(namelist, files_count);
	    return 1;
	}

	for (i = 0; i < files_count; ++i) {
	    assert(namelist[i] != NULL);
	    oldnumlogs = numLogs;
	    copyLogInfo(&defConfigBackup, defConfig);
	    if (readConfigFile(namelist[i], defConfig)) {
		message(MESS_ERROR, "found error in file %s, skipping\n", namelist[i]);
		freeTailLogs(numLogs - oldnumlogs);
		freeLogInfo(defConfig);
		copyLogInfo(defConfig, &defConfigBackup);
		freeLogInfo(&defConfigBackup);
		continue;
	    } else {
		result = 0;
	    }
	    freeLogInfo(&defConfigBackup);
	}

	fchdir(here);
	close(here);
	free_2d_array(namelist, files_count);
    } else {
    	oldnumlogs = numLogs;
	copyLogInfo(&defConfigBackup, defConfig);
	if (readConfigFile(path, defConfig)) {
	    freeTailLogs(numLogs - oldnumlogs);
	    freeLogInfo(defConfig);
	    copyLogInfo(defConfig, &defConfigBackup);
	} else {
	    result = 0;
	}
	freeLogInfo(&defConfigBackup);
    }

    return result;
}

int readAllConfigPaths(const char **paths)
{
    int i, result = 0;
    const char **file;
    struct logInfo defConfig = { /* pattern */ NULL,
	/* files, numFiles */ NULL, 0,
	/* oldDir */ NULL,
	/* criterium */ ROT_SIZE,
	/* threshHold */ 1024 * 1024,
	/* minsize */ 0,
	/* rotateCount/Age */ 0, 0,
	/* log start */ -1,
	/* pre, post */ NULL, NULL,
	/* first, last */ NULL, NULL,
	/* logAddress */ NULL,
	/* extension */ NULL,
	/* compress_prog */ NULL,
	/* uncompress_prog */ NULL,
	/* compress_ext */ NULL,
	/* dateformat */ NULL,
	/* flags */ LOG_FLAG_IFEMPTY,
	/* shred_cycles */ 0,
	/* createMode/Uid/Gid */ NO_MODE, NO_UID, NO_GID,
	/* compress_options_list/count */ NULL, 0
    };

    tabooExts = malloc(sizeof(*tabooExts) * defTabooCount);
    for (i = 0; i < defTabooCount; i++) {
	if ((tabooExts[i] = (char *) malloc(strlen(defTabooExts[i]) + 1))) {
	    strcpy(tabooExts[i], defTabooExts[i]);
	    tabooCount++;
	} else {
	    free_2d_array(tabooExts, tabooCount);
	    message(MESS_ERROR, "cannot malloc: %s\n", strerror(errno));
	    return 1;
	}
    }

    for (file = paths; *file; file++) {
	if (readConfigPath(*file, &defConfig)) {
	    result = 1;
	    break;
	}
    }
    free_2d_array(tabooExts, tabooCount);
    freeLogInfo(&defConfig);
    return result;
}

static int globerr(const char *pathname, int theerr)
{
    message(MESS_ERROR, "error accessing %s: %s\n", pathname,
	    strerror(theerr));

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

#define freeLogItem(what) \
    if (newlog->what) { \
	free(newlog->what); \
    	newlog->what = NULL; \
    }

static int readConfigFile(const char *configFile, struct logInfo *defConfig)
{
    int fd;
    char *buf, *endtag;
    char oldchar, foo;
    off_t length;
    int lineNum = 1;
    int multiplier;
    int i, k;
    char *scriptStart = NULL;
    char **scriptDest = NULL;
    struct logInfo *newlog = defConfig;
    char *start, *chptr;
    char *dirName;
    struct group *group;
    struct passwd *pw;
    int rc;
    char createOwner[200], createGroup[200];
    int createMode;
    struct stat sb, sb2;
    glob_t globResult;
    const char **argv;
    int argc, argNum;
    int logerror = 0;
    struct logInfo *log;

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
	message(MESS_DEBUG,
		"Ignoring %s because it's not a regular file.\n",
		configFile);
	close(fd);
	return 0;
    }

    length = sb.st_size;

    buf = alloca(length + 2);
    if (!buf) {
	message(MESS_ERROR, "alloca() of %d bytes failed\n", (int) length);
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
	if (logerror) {
	    assert(newlog != defConfig);

	    message(MESS_ERROR, "found error in %s, skipping\n",
		    newlog->pattern ? newlog->pattern : "log config");

	    while (*start != '}') {
		if (*start == 0) {
		    message(MESS_ERROR, "%s:%d } expected \n",
			    configFile, lineNum);
		    return 1;
		} else if (*start == '\n') {
		    lineNum++;
		}
		start++;
	    }
	    start++;

	    freeTailLogs(1);
	    newlog = defConfig;
	    logerror = 0;
	}				
	while (isblank(*start) && (*start))
	    start++;
	if (*start == '#') {
	    while (*start != '\n')
		start++;
	}

	if (*start == '\n') {
	    start++;
	    lineNum++;
	    continue;
	}

	if (scriptStart) {
	    if (!strncmp(start, "endscript", 9)) {
		chptr = start + 9;
		while (isblank(*chptr))
		    chptr++;
		if (*chptr == '\n') {
		    endtag = start;
		    while (*endtag != '\n')
			endtag--;
		    endtag++;
		    *scriptDest = malloc(endtag - scriptStart + 1);
		    strncpy(*scriptDest, scriptStart,
			    endtag - scriptStart);
		    (*scriptDest)[endtag - scriptStart] = '\0';
		    start = chptr + 1;
		    lineNum++;

		    scriptDest = NULL;
		    scriptStart = NULL;
		}
	    }

	    if (scriptStart) {
		while (*start != '\n')
		    start++;
		lineNum++;
		start++;
	    }
	} else if (isalpha(*start)) {
	    endtag = start;
	    while (isalpha(*endtag))
		endtag++;
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
		} else if (!strcmp(start, "shred")) {
		newlog->flags |= LOG_FLAG_SHRED;

		*endtag = oldchar, start = endtag;
		} else if (!strcmp(start, "noshred")) { 
		newlog->flags &= ~LOG_FLAG_SHRED;

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
	    } else if (!strcmp(start, "dateext")) {
		newlog->flags |= LOG_FLAG_DATEEXT;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nodateext")) {
		newlog->flags &= ~LOG_FLAG_DATEEXT;
		
		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "dateformat")) {
		*endtag = oldchar, start = endtag;
		
		endtag = start;
		while (*endtag != '\n')
		    endtag++;
		while (isspace(*endtag))
		    endtag--;
		endtag++;
		oldchar = *endtag, *endtag = '\0';

		freeLogItem(dateformat);
		newlog->dateformat = strdup(start);

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
		while (*endtag != '\n')
		    endtag++;
		while (isspace(*endtag))
		    endtag--;
		endtag++;
		oldchar = *endtag, *endtag = '\0';

		rc = sscanf(start, "%o %s %s%c", &createMode,
			    createOwner, createGroup, &foo);
		if (rc == 4) {
		    message(MESS_ERROR, "%s:%d extra arguments for "
			    "create\n", configFile, lineNum);
		    if (newlog != defConfig) {
			*endtag = oldchar, start = endtag;
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		if (rc > 0)
		    newlog->createMode = createMode;

		if (rc > 1) {
		    pw = getpwnam(createOwner);
		    if (!pw) {
			message(MESS_ERROR, "%s:%d unknown user '%s'\n",
				configFile, lineNum, createOwner);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }
		    newlog->createUid = pw->pw_uid;
		    endpwent();
		}
		if (rc > 2) {
		    group = getgrnam(createGroup);
		    if (!group) {
			message(MESS_ERROR, "%s:%d unknown group '%s'\n",
				configFile, lineNum, createGroup);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }
		    newlog->createGid = group->gr_gid;
		    endgrent();
		}

		newlog->flags |= LOG_FLAG_CREATE;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nocreate")) {
		newlog->flags &= ~LOG_FLAG_CREATE;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "size") || !strcmp(start, "minsize")) {
		unsigned int size = 0;
		char *opt = start;
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, opt, &start,
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
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    } else {
			multiplier = 1;
		    }

		    size = multiplier * strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MESS_ERROR, "%s:%d bad size '%s'\n",
				configFile, lineNum, start);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }

		    if (!strncmp(opt, "size", 4)) {
			newlog->criterium = ROT_SIZE;
			newlog->threshhold = size;
		    } else
			newlog->minsize = size;

		    *endtag = oldchar, start = endtag;
		}
#if 0				/* this seems like such a good idea :-( */
	    } else if (!strcmp(start, "days")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "size", &start,
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->threshhold = strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MESS_ERROR,
				"%s:%d bad number of days'%s'\n",
				configFile, lineNum, start);
			return 1;
		    }

		    newlog->criterium = ROT_DAYS;

		    *endtag = oldchar, start = endtag;
		}
#endif
	    } else if (!strcmp(start, "shredcycles")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue(configFile, lineNum, "shred cycles", 
				&start, &endtag)) {
			oldchar = *endtag, *endtag = '\0';

			newlog->shred_cycles = strtoul(start, &chptr, 0);
			if (*chptr || newlog->shred_cycles < 0) {
				message(MESS_ERROR, "%s:%d bad shred cycles '%s'\n",
						configFile, lineNum, start);
				return 1;
			}
			*endtag = oldchar, start = endtag;
		}
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
	    } else if (!strcmp(start, "yearly")) {
		*endtag = oldchar, start = endtag;

		newlog->criterium = ROT_YEARLY;
	    } else if (!strcmp(start, "rotate")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue
		    (configFile, lineNum, "rotate count", &start,
		     &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->rotateCount = strtoul(start, &chptr, 0);
		    if (*chptr || newlog->rotateCount < 0) {
			message(MESS_ERROR,
				"%s:%d bad rotation count '%s'\n",
				configFile, lineNum, start);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }
		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "start")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue
		    (configFile, lineNum, "start count", &start,
		     &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->logStart = strtoul(start, &chptr, 0);
		    if (*chptr || newlog->logStart < 0) {
			message(MESS_ERROR, "%s:%d bad start count '%s'\n",
				configFile, lineNum, start);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }
		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "maxage")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue
		    (configFile, lineNum, "maxage count", &start,
		     &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->rotateAge = strtoul(start, &chptr, 0);
		    if (*chptr || newlog->rotateAge < 0) {
			message(MESS_ERROR, "%s:%d bad maximum age '%s'\n",
				configFile, lineNum, start);
			if (newlog != defConfig) {
			    *endtag = oldchar, start = endtag;
			    logerror = 1;
			    continue;
			} else {
			    return 1;
			}
		    }
		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "errors")) {
		message(MESS_DEBUG,
			"%s: %d: the errors directive is deprecated and no longer used.\n",
			configFile, lineNum);
	    } else if (!strcmp(start, "mail")) {
		*endtag = oldchar, start = endtag;
		freeLogItem(logAddress);
		if (!(newlog->logAddress = readAddress(configFile, lineNum,
						       "mail", &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}
	    } else if (!strcmp(start, "nomail")) {
	        freeLogItem(logAddress);

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "missingok")) {
		newlog->flags |= LOG_FLAG_MISSINGOK;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "nomissingok")) {
		newlog->flags &= ~LOG_FLAG_MISSINGOK;

		*endtag = oldchar, start = endtag;
	    } else if (!strcmp(start, "prerotate")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (pre);

		scriptStart = start;
		scriptDest = &newlog->pre;

		while (*start != '\n')
		    start++;
	    } else if (!strcmp(start, "firstaction")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (first);

		scriptStart = start;
		scriptDest = &newlog->first;

		while (*start != '\n')
		    start++;
	    } else if (!strcmp(start, "postrotate")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (post);

		scriptStart = start;
		scriptDest = &newlog->post;

		while (*start != '\n')
		    start++;
	    } else if (!strcmp(start, "lastaction")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (last);

		scriptStart = start;
		scriptDest = &newlog->last;

		while (*start != '\n')
		    start++;
	    } else if (!strcmp(start, "tabooext")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR,
			    "%s:%d tabooext may not appear inside "
			    "of log file definition\n", configFile,
			    lineNum);
		    *endtag = oldchar, start = endtag;
		    logerror = 1;
		    continue;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "tabooext", &start,
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    if (*start == '+') {
			start++;
			while (isspace(*start) && *start)
			    start++;
		    } else {
			free_2d_array(tabooExts, tabooCount);
			tabooCount = 0;
			tabooExts = malloc(1);
		    }

		    while (*start) {
			chptr = start;
			while (!isspace(*chptr) && *chptr != ',' && *chptr)
			    chptr++;

			tabooExts = realloc(tabooExts, sizeof(*tabooExts) *
					    (tabooCount + 1));
			tabooExts[tabooCount] = malloc(chptr - start + 1);
			strncpy(tabooExts[tabooCount], start,
				chptr - start);
			tabooExts[tabooCount][chptr - start] = '\0';
			tabooCount++;

			start = chptr;
			if (*start == ',')
			    start++;
			while (isspace(*start) && *start)
			    start++;
		    }

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "include")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR,
			    "%s:%d include may not appear inside "
			    "of log file definition\n", configFile,
			    lineNum);
		    *endtag = oldchar, start = endtag;
		    logerror = 1;
		    continue;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "include", &start,
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    message(MESS_DEBUG, "including %s\n", start);

		    if (readConfigPath(start, defConfig))
			return 1;

		    *endtag = oldchar, start = endtag;
		}
	    } else if (!strcmp(start, "olddir")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (oldDir);

		if (!(newlog->oldDir = readPath(configFile, lineNum,
						"olddir", &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}
#if 0
		if (stat(newlog->oldDir, &sb)) {
		    message(MESS_ERROR, "%s:%d error verifying olddir "
			    "path %s: %s\n", configFile, lineNum,
			    newlog->oldDir, strerror(errno));
		    free(newlog->oldDir);
		    return 1;
		}

		if (!S_ISDIR(sb.st_mode)) {
		    message(MESS_ERROR, "%s:%d olddir path %s is not a "
			    "directory\n", configFile, lineNum,
			    newlog->oldDir);
		    free(newlog->oldDir);
		    return 1;
		}
#endif

		message(MESS_DEBUG, "olddir is now %s\n", newlog->oldDir);
	    } else if (!strcmp(start, "extension")) {
		*endtag = oldchar, start = endtag;

		if (!isolateValue
		    (configFile, lineNum, "extension name", &start,
		     &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    freeLogItem (extension);
		    newlog->extension = strdup(start);

		    *endtag = oldchar, start = endtag;
		}

		message(MESS_DEBUG, "extension is now %s\n",
			newlog->extension);

	    } else if (!strcmp(start, "compresscmd")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (compress_prog);

		if (!
		    (newlog->compress_prog =
		     readPath(configFile, lineNum, "compress", &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		if (access(newlog->compress_prog, X_OK)) {
		    message(MESS_ERROR,
			    "%s:%d compression program %s is not an executable file\n",
			    configFile, lineNum, newlog->compress_prog);
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		message(MESS_DEBUG, "compress_prog is now %s\n",
			newlog->compress_prog);

	    } else if (!strcmp(start, "uncompresscmd")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (uncompress_prog);

		if (!
		    (newlog->uncompress_prog =
		     readPath(configFile, lineNum, "uncompress",
			      &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		if (access(newlog->uncompress_prog, X_OK)) {
		    message(MESS_ERROR,
			    "%s:%d uncompression program %s is not an executable file\n",
			    configFile, lineNum, newlog->uncompress_prog);
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		message(MESS_DEBUG, "uncompress_prog is now %s\n",
			newlog->uncompress_prog);

	    } else if (!strcmp(start, "compressoptions")) {
		char *options;

		if (newlog->compress_options_list) {
		    free(newlog->compress_options_list);
		    newlog->compress_options_list = NULL;
		    newlog->compress_options_count = 0;
		}

		*endtag = oldchar, start = endtag;
		if (!
		    (options =
		     readPath(configFile, lineNum, "compressoptions",
			      &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		if (poptParseArgvString(options,
					&newlog->compress_options_count,
					&newlog->compress_options_list)) {
		    message(MESS_ERROR,
			    "%s:%d invalid compression options\n",
			    configFile, lineNum);
		    free(options);
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		message(MESS_DEBUG, "compress_options is now %s\n",
			options);
		free(options);
	    } else if (!strcmp(start, "compressext")) {
		*endtag = oldchar, start = endtag;

		freeLogItem (compress_ext);

		if (!
		    (newlog->compress_ext =
		     readPath(configFile, lineNum, "compress-ext",
			      &start))) {
		    if (newlog != defConfig) {
			logerror = 1;
			continue;
		    } else {
			return 1;
		    }
		}

		message(MESS_DEBUG, "compress_ext is now %s\n",
			newlog->compress_ext);
	    } else {
		message(MESS_ERROR, "%s:%d unknown option '%s' "
			"-- ignoring line\n", configFile, lineNum, start);

		*endtag = oldchar, start = endtag;
	    }

	    while (isblank(*start))
		start++;

	    if (*start != '\n') {
		message(MESS_ERROR, "%s:%d unexpected text\n", configFile,
			lineNum);
		while (*start != '\n')
		    start++;
	    }

	    lineNum++;
	    start++;
	} else if (*start == '/' || *start == '"' || *start == '\'') {
	    if (newlog != defConfig) {
		message(MESS_ERROR, "%s:%d unexpected log filename\n",
			configFile, lineNum);
		logerror = 1;
		continue;
	    }

	    /* If no compression options were found in config file, set
	       default values */
	    if (!newlog->compress_prog)
		newlog->compress_prog = strdup(COMPRESS_COMMAND);
	    if (!newlog->uncompress_prog)
		newlog->uncompress_prog = strdup(UNCOMPRESS_COMMAND);
	    if (!newlog->compress_ext)
		newlog->compress_ext = strdup(COMPRESS_EXT);

	    /* Allocate a new logInfo structure and insert it into the logs
	       queue, copying the actual values from defConfig */
	    if ((newlog = newLogInfo(defConfig)) == NULL)
		return 1;

	    endtag = start;
	    while (*endtag != '{' && *endtag != '\0')
		endtag++;
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
		message(MESS_ERROR,
			"%s:%d { expected after log file name(s)\n",
			configFile, lineNum);
		return 1;
	    }

	    newlog->files = NULL;
	    newlog->numFiles = 0;
	    for (argNum = 0; argNum < argc && logerror != 1; argNum++) {
		rc = glob(argv[argNum], GLOB_NOCHECK, globerr,
			  &globResult);
		if (rc == GLOB_ABORTED) {
		    if (newlog->flags & LOG_FLAG_MISSINGOK)
			continue;

		    message(MESS_ERROR, "%s:%d glob failed for %s\n",
			    configFile, lineNum, argv[argNum]);
		    logerror = 1;
		    break;
		}

		newlog->files =
		    realloc(newlog->files,
			    sizeof(*newlog->files) * (newlog->numFiles +
						      globResult.
						      gl_pathc));

		for (i = 0; i < globResult.gl_pathc; i++) {
		    /* if we glob directories we can get false matches */
		    if (!lstat(globResult.gl_pathv[i], &sb) &&
			S_ISDIR(sb.st_mode))
			continue;

		    for (log = logs.tqh_first; log != NULL;
				log = log->list.tqe_next) {
			for (k = 0; k < log->numFiles; k++) {
			    if (!strcmp(log->files[k],
					globResult.gl_pathv[i])) {
				message(MESS_ERROR,
					"%s:%d duplicate log entry for %s\n",
					configFile, lineNum,
					globResult.gl_pathv[i]);
				logerror = 1;
				goto duperror;
			    }
			}
		    }

		    newlog->files[newlog->numFiles] =
			strdup(globResult.gl_pathv[i]);
		    newlog->numFiles++;
		}
duperror:
		globfree(&globResult);
	    }

	    newlog->pattern = strdup(start);

	    if (!logerror)
		message(MESS_DEBUG, "reading config info for %s\n", start);

	    free(argv);

	    start = endtag + 1;
	} else if (*start == '}') {
	    if (newlog == defConfig) {
		message(MESS_ERROR, "%s:%d unexpected }\n", configFile,
			lineNum);
		return 1;
	    }

	    if (newlog->oldDir) {
		for (i = 0; i < newlog->numFiles; i++) {
		    char *ld;
		    dirName = ourDirName(newlog->files[i]);
		    if (stat(dirName, &sb2)) {
			message(MESS_ERROR,
				"%s:%d error verifying log file "
				"path %s: %s\n", configFile, lineNum,
				dirName, strerror(errno));
			free(dirName);
			return 1;
		    }
		    ld = alloca(strlen(dirName) + strlen(newlog->oldDir) +
				2);
		    sprintf(ld, "%s/%s", dirName, newlog->oldDir);
		    free(dirName);

		    if (newlog->oldDir[0] != '/')
			dirName = ld;
		    else
			dirName = newlog->oldDir;
		    if (stat(dirName, &sb)) {
			message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum,
				dirName, strerror(errno));
			return 1;
		    }

		    if (sb.st_dev != sb2.st_dev) {
			message(MESS_ERROR,
				"%s:%d olddir %s and log file %s "
				"are on different devices\n", configFile,
				lineNum, newlog->oldDir, newlog->files[i]);
			return 1;
		    }
		}
	    }

	    newlog = defConfig;

	    start++;
	    while (isblank(*start))
		start++;

	    if (*start != '\n') {
		message(MESS_ERROR, "%s:%d, unexpected text after {\n",
			configFile, lineNum);
	    }
	} else {
	    message(MESS_ERROR, "%s:%d lines must begin with a keyword "
		    "or a filename (possibly in double quotes)\n",
		    configFile, lineNum);

	    while (*start != '\n')
		start++;
	    lineNum++;
	    start++;
	}
    }

    if (scriptStart) {
	message(MESS_ERROR,
		"%s:prerotate or postrotate without endscript\n",
		configFile);
	return 1;
    }

    return 0;
}
