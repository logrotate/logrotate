#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

#define LOG_FLAG_COMPRESS	(1 << 0)

#define COMPRESS_COMMAND "gzip -9"
#define COMPRESS_EXT ".gz"
#define UNCOMPRESS_PIPE "gunzip"

typedef struct {
    char * fn;
    enum { ROT_DAYS, ROT_WEEKLY, ROT_MONTHLY, ROT_SIZE } criterium;
    unsigned int threshhold;
    int rotateCount;
    char * pre, * post;
    char * logAddress;
    char * errAddress;
    int flags;
} logInfo;

int debug = 0;

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

static int readConfigPath(char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr) {
    struct stat sb;
    DIR * dir;
    struct dirent * ent;
    int here;

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
		if (readConfigFile(ent->d_name, defConfig, logsPtr, 
				   numLogsPtr)) {
		    fchdir(here);
		    close(here);
		    return 1;
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
    char oldchar;
    int length;
    int lineNum = 1;
    int multiplier;
    char * scriptStart = NULL;
    char ** scriptDest = NULL;
    logInfo * newlog = defConfig;
    char * start, * chptr;

    fd = open(configFile, O_RDONLY);
    if (fd < 0) {
	message(MESS_ERROR, "failed to open config file %s: %s\n",
		configFile, strerror(errno));
	return 1;
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

int rotateLog(logInfo * log) {
    struct stat sb;
    time_t nowSecs = time(NULL);
    struct tm now = *localtime(&nowSecs);
    struct tm ctime;
    FILE * errorFile;
    char * errorFileName;
    char * oldName, * newName;
    char * disposeName;
    char * finalName;
    char * tmp;
    char * ext = "";
    int hasErrors = 0;
    int doRotate;
    int i;
    int oldstderr, newerr;

    message(MESS_DEBUG, "rotating: %s ", log->fn);
    switch (log->criterium) {
      case ROT_DAYS:
	message(MESS_DEBUG, "after %d days ", log->threshhold);
	break;
      case ROT_WEEKLY:
	message(MESS_DEBUG, "weekly ");
	break;
      case ROT_MONTHLY:
	message(MESS_DEBUG, "monthly ");
	break;
      case ROT_SIZE:
	message(MESS_DEBUG, "bytes ");
	break;
    }

    if (log->rotateCount) 
	message(MESS_DEBUG, "(%d rotations)\n", log->rotateCount);
    else
	message(MESS_DEBUG, "(no old logs will be kept)\n");

    if (log->flags & LOG_FLAG_COMPRESS) ext = COMPRESS_EXT;

    if (log->errAddress) {
	message(MESS_DEBUG, "errors mailed to %s ", log->errAddress);
	errorFileName = strdup(tmpnam(NULL));

	newerr = open(errorFileName, O_WRONLY | O_CREAT, 0600);

	if (newerr < 0) {
	    message(MESS_ERROR, "error creating temporary file %s\n",
			errorFileName);
	    return 1;
	}
	
	oldstderr = dup(2);
	dup2(newerr, 2);
	close(newerr);

	errorFile = fdopen(2, "w");

	setlinebuf(errorFile);
	fprintf(errorFile, "errors occured while rotating %s\n\n",
		log->fn);
    } else {
	message(MESS_DEBUG, "errors displayed on stderr ");
	errorFile = stderr;
    }

    if (log->logAddress) {
	message(MESS_DEBUG, "old logs mailed to %s\n", log->logAddress);
    } else {
	message(MESS_DEBUG, "old logs are removed\n");
    }
    
    if (stat(log->fn, &sb)) {
	fprintf(errorFile, "stat of %s failed: %s\n", log->fn,
		strerror(errno));
	hasErrors = 1;
    }

    if (!hasErrors) {
	ctime = *localtime(&sb.st_ctime);

	if (log->criterium == ROT_SIZE) {
	    doRotate = (sb.st_size >= log->threshhold);
	} else if (ctime.tm_year != now.tm_year || 
		   ctime.tm_yday != now.tm_yday) {
	    switch (log->criterium) {
	      case ROT_WEEKLY:
		doRotate = !now.tm_wday;
		break;
	      case ROT_MONTHLY:
		doRotate = (now.tm_mday == 1);
		break;
	      case ROT_DAYS:
		/* FIXME: only days=1 is implemented!! */
		doRotate = 1;
	      default:
		/* ack! */
		doRotate = 0;
		break;
	    }
	} else {
	    doRotate = 0;
	}
    }

    if (!hasErrors && doRotate) {
	message(MESS_DEBUG, "log needs rotating\n");

	if (!log->rotateCount) {
	    disposeName = log->fn;
	    finalName = NULL;
	} else {
	    oldName = alloca(strlen(log->fn) + 10);
	    newName = alloca(strlen(log->fn) + 10);

	    sprintf(oldName, "%s.%d%s", log->fn, log->rotateCount + 1, ext);

	    disposeName = alloca(strlen(log->fn) + 10);
	    strcpy(disposeName, oldName);

	    for (i = log->rotateCount; i && !hasErrors; i--) {
		tmp = newName;
		newName = oldName;
		oldName = tmp;
		sprintf(oldName, "%s.%d%s", log->fn, i, ext);

		message(MESS_DEBUG, "renaming %s to %s\n", oldName, newName);

		if (!debug && rename(oldName, newName)) {
		    if (errno == ENOENT) {
			message(MESS_DEBUG, "old log %s does not exist\n",
				oldName);
		    } else {
			fprintf(errorFile, "error renaming %s to %s: %s\n",
				oldName, newName, strerror(errno));
			hasErrors = 1;
		    }
		}
	    }

	    finalName = oldName;

	    /* note: the gzip extension is *not* used here! */
	    sprintf(finalName, "%s.1", log->fn);

	    /* if the last rotation doesn't exist, that's okay */
	    if (!debug && access(disposeName, F_OK)) {
		message(MESS_DEBUG, "file %s doesn't exist -- won't try "
			"dispose of it\n", disposeName);
		disposeName = NULL;
	    } 
	}

	if (!hasErrors && log->logAddress && disposeName) {
	    char * command;

	    command = alloca(strlen(disposeName) + 100 + 
			     strlen(UNCOMPRESS_PIPE));

	    if (log->flags & LOG_FLAG_COMPRESS)
		sprintf(command, "%s < %s | /bin/mail -s '%s' %s", 
			    UNCOMPRESS_PIPE, disposeName, log->fn,
			    log->logAddress);
	    else
		sprintf(command, "/bin/mail -s '%s' %s < %s", disposeName, 
			    log->logAddress, disposeName);

	    message(MESS_DEBUG, "executing: \"%s\"\n", command);

	    if (!debug && system(command)) {
		sprintf(newName, "%s.%d", log->fn, getpid());
		fprintf(errorFile, "Failed to mail %s to %s!\n",
			disposeName, log->logAddress);

		hasErrors = 1;
	    } 
	}

	if (!hasErrors && disposeName) {
	    message(MESS_DEBUG, "removing old log %s\n", disposeName);

	    if (!debug && unlink(disposeName)) {
		fprintf(errorFile, "Failed to remove old log %s: %s\n",
			    disposeName, strerror(errno));
		hasErrors = 1;
	    }
	}

	if (!hasErrors && finalName) {
	    if (log->pre) {
		message(MESS_DEBUG, "running prerotate script\n");
		if (!debug && system(log->pre)) {
		    fprintf(errorFile, "error running prerotate script -- 
				leaving old log in place\n");
		    hasErrors = 1;
		}
	    }

	    message(MESS_DEBUG, "renaming %s to %s\n", log->fn, finalName);

	    if (!debug && !hasErrors && rename(log->fn, finalName)) {
		fprintf(errorFile, "failed to rename %s to %s: %s\n",
			log->fn, finalName, strerror(errno));
	    }

	    if (!hasErrors && log->post) {
		message(MESS_DEBUG, "running postrotate script\n");
		if (!debug && system(log->post)) {
		    fprintf(errorFile, "error running postrotate script\n");
		    hasErrors = 1;
		}
	    }

	    if (!hasErrors && log->flags & LOG_FLAG_COMPRESS) {
		char * command;

		command = alloca(strlen(finalName) + strlen(COMPRESS_COMMAND)
				 + 20);

		sprintf(command, "%s %s", COMPRESS_COMMAND, finalName);
		message(MESS_DEBUG, "compressing new log with: %s\n", command);
		if (!debug && system(command)) {
		    fprintf(errorFile, "failed to compress log %s\n", 
				finalName);
		    hasErrors = 1;
		}
	    }
	}
    } else if (!doRotate) {
	message(MESS_DEBUG, "log does not need rotating\n");
    }

    if (log->errAddress) {
	fclose(errorFile);

	close(2);
	dup2(oldstderr, 2);
	close(oldstderr);

	if (hasErrors) {
	    char * command;

	    command = alloca(strlen(errorFileName) + strlen(log->errAddress) +
				50);
	    sprintf(command, "/bin/mail -s 'errors rotating logs' %s < %s\n",
		    log->errAddress, errorFileName);
	    if (system(command)) {
		message(MESS_ERROR, "error mailing error log to %s -- errors "
			"left in %s\n", log->errAddress, errorFileName);
	    }
	}

	unlink(errorFileName);
    }

    return hasErrors;
}

int main(int argc, char ** argv) {
    logInfo defConfig = { NULL, ROT_SIZE, 1024 * 1024, 0, 0, NULL, 
			  NULL, NULL, 0 };
    int numLogs = 0;
    logInfo * logs = NULL;
    int i;
    int rc;
    int arg, long_index;
    struct option options[] = {
	{ "debug", 0, 0, 'd' },
	{ "verbose", 0, 0, 'v' }
    };

    logSetLevel(MESS_NORMAL);

    while (1) {
        arg = getopt_long(argc, argv, "dv", options, &long_index);
        if (arg == -1) break;

        switch (arg) {
	  case 'd':
	    debug = 1;
	    /* fallthrough */
	  case 'v':
	    logSetLevel(MESS_DEBUG);
	    break;

          case '?':
            fprintf(stderr, "usage: logrotate [-dv] <config_files>\n");
            exit(1);
            break;
	}
    }

    if (optind == argc) {
	fprintf(stderr, "usage: logrotate [-dv] <config_files>\n");
	exit(1);
    }

    while (optind < argc) {
	if (readConfigPath(argv[optind], &defConfig, &logs, &numLogs)) {
	    exit(1);
	}
	optind++;
    }

    message(MESS_DEBUG, "Handling %d logs\n", numLogs);

    for (i = 0; i < numLogs; i++) {
	rc |= rotateLog(logs + i);
    }

    return (rc != 0);
}
