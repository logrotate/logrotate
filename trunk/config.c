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

static char * defTabooExts[] = { ".rpmsave", ".rpmorig", "~", ",v",
				 ".rpmnew", ".swp" };
static int defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char ** tabooExts = NULL;
int tabooCount = 0;

static int readConfigFile(const char * configFile, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);
static int globerr(const char * pathname, int theerr);

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

/** This function will be called as the 'select' method in the scandir(3)
    function-call. */
static int
checkFilelist(struct dirent const *ent)
{
    size_t	i;
    assert(ent!=0);

    /* Check if ent->d_name is '.' or '..'; if so, return false */
    if (ent->d_name[0] == '.' &&
	(!ent->d_name[1] || (ent->d_name[1] == '.' && !ent->d_name[2])))
        return 0;

    /* Check if ent->d_name is ending in a taboo-extension; if so, return
       false */
    for (i = 0; i < tabooCount; i++) {
        if (!strcmp(ent->d_name + strlen(ent->d_name) -
		    strlen(tabooExts[i]), tabooExts[i])) {
	    message(MESS_DEBUG, "Ignoring %s, because of %s "
		    "ending\n", ent->d_name, tabooExts[i]);

	    return 0;
	}
    }

      /* All checks have been passed; return true */
    return 1;
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
        struct dirent	**namelist;
	int		files_count,i;
      
	here = open(".", O_RDONLY);
	if (here < 0) {
	    message(MESS_ERROR, "cannot open current directory: %s\n", 
		    strerror(errno));
	    return 1;
	}

	files_count = scandir(path, &namelist, checkFilelist, alphasort);
	if (files_count==-1) {
  	    message(MESS_ERROR, "scandir(): %s", strerror(errno));
	    return 1;
	}
	
	if (chdir(path)) {
	    message(MESS_ERROR, "error in chdir(\"%s\"): %s\n", path,
		    strerror(errno));
	    close(here);
	    free(namelist);
	    return 1;
	}

	for (i=0; i<files_count; ++i) {
	  assert(namelist[i]!=0);
	  
	  if (readConfigFile(namelist[i]->d_name, defConfig, logsPtr, 
			     numLogsPtr)) {
	    fchdir(here);
	    close(here);
	    free(namelist);
	    return 1;
	  }
	};

	fchdir(here);
	close(here);
	free(namelist);
    } else {
	return readConfigFile(path, defConfig, logsPtr, numLogsPtr);
    }

    return 0;
}

static int globerr(const char * pathname, int theerr) {
    message(MESS_ERROR, "error accessing %s: %s\n", pathname, 
            strerror(theerr));

    /* We want the glob operation to continue, so return 0 */
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
	    } else if (!strcmp(start, "postrotate")) {
		*endtag = oldchar, start = endtag;

		scriptStart = start;
		scriptDest = &newlog->post;

		while (*start != '\n') start++;
	    } else if (!strcmp(start, "tabooext")) {
		if (newlog != defConfig) {
		    message(MESS_ERROR, "%s:%d tabooext may not appear inside "
			    "of log file definition\n", configFile, lineNum);
		    return 1;
		}

		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "size", &start, 
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
				"directory\n", configFile, lineNum, 
				newlog->oldDir);
		    return 1;
		}

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
		*endtag = oldchar, start = endtag;
		if (!(newlog->compress_options = readPath(configFile, lineNum, "compressoptions", &start))) {
		    return 1;
		}

		message(MESS_DEBUG, "compress_options is now %s\n", newlog->compress_options);

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
		if (stat(newlog->oldDir, &sb)) {
		    message(MESS_ERROR, "%s:%d error verifying olddir "
				"path %s: %s\n", configFile, lineNum, 
				newlog->oldDir, strerror(errno));
		    return 1;
		}

		for (i = 0; i < newlog->numFiles; i++) {
		    dirName = ourDirName(newlog->files[i]);
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

    return 0;
}

