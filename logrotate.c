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
#include "logrotate.h"

typedef struct {
    char * fn;
    struct tm lastRotated;	/* only tm.mon, tm_mday, tm_year are good! */
} logState;

int debug = 0;

static logState * findState(char * fn, logState ** statesPtr, 
			    int * numStatesPtr) {
    int i;
    logState * states = *statesPtr;
    int numStates = *numStatesPtr;

    for (i = 0; i < numStates; i++) 
	if (!strcmp(fn, states[i].fn)) break;

    if (i == numStates) {
	i = numStates++;
	states = realloc(states, sizeof(*states) * numStates);
	states[i].fn = strdup(fn);
	memset(&states[i].lastRotated, 0, sizeof(states[i].lastRotated));

	*statesPtr = states;
	*numStatesPtr = numStates;
    }

    return (states + i);
}

int rotateLog(logInfo * log, logState ** statesPtr, int * numStatesPtr) {
    struct stat sb;
    time_t nowSecs = time(NULL);
    struct tm now = *localtime(&nowSecs);
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
    logState * state;

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
	state = findState(log->fn, statesPtr, numStatesPtr);

	if (log->criterium == ROT_SIZE) {
	    doRotate = (sb.st_size >= log->threshhold);
	} else if (state->lastRotated.tm_year != now.tm_year || 
		   state->lastRotated.tm_mon != now.tm_mon ||
		   state->lastRotated.tm_mday != now.tm_mday) {
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
		break;
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

	state->lastRotated = now;

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

static int writeState(char * stateFilename, logState * states, 
		      int numStates) {
    FILE * f;
    int i;

    f = fopen(stateFilename, "w");
    if (!f) {
	message(MESS_ERROR, "error creating state file %s: %s\n", 
		    stateFilename, strerror(errno));
	return 1;
    }

    fprintf(f, "logrotate state -- version 1\n");

    for (i = 0; i < numStates; i++) {
	fprintf(f, "%s %d-%d-%d\n", states[i].fn, 
		states[i].lastRotated.tm_year + 1900,
		states[i].lastRotated.tm_mon + 1,
		states[i].lastRotated.tm_mday);
    }

    fclose(f);

    return 0;
}

static int readState(char * stateFilename, logState ** statesPtr, 
		     int * numStatesPtr) {
    FILE * f;
    char buf[1024];
    char buf2[1024];
    int year, month, day;
    int i;
    int line = 0;
    logState * st;

    f = fopen(stateFilename, "r");

    if (!f && errno == ENOENT) {
	/* create the file before continuing to ensure we have write
	   access to the file */
	f = fopen(stateFilename, "w");
	if (!f) {
	    message(MESS_ERROR, "error creating state file %s: %s\n", 
			stateFilename, strerror(errno));
	    return 1;
	}
	fprintf(f, "logrotate state -- version 1\n");
	fclose(f);
	return 0;
    } else if (!f) {
	message(MESS_ERROR, "error creating state file %s: %s\n", 
		    stateFilename, strerror(errno));
	return 1;
    }

    if (!fgets(buf, sizeof(buf) - 1, f)) {
	message(MESS_ERROR, "error reading top line of %s\n", stateFilename);
	fclose(f);
	return 1;
    }

    if (strcmp(buf, "logrotate state -- version 1\n")) {
	fclose(f);
	message(MESS_ERROR, "bad top line in state file %s\n", stateFilename);
	return 1;
    }

    line++;

    while (fgets(buf, sizeof(buf) - 1, f)) {
	line++;
	i = strlen(buf);
	if (buf[i - 1] != '\n') {
	    message(MESS_ERROR, "line too long in state file %s\n", 
			stateFilename);
	    fclose(f);
	    return 1;
	}

	if (i == 1) continue;

	if (sscanf(buf, "%s %d-%d-%d\n", buf2, &year, &month, &day) != 4) {
	    message(MESS_ERROR, "bad line %d in state file %s\n", 
		    stateFilename);
	    fclose(f);
	    return 1;
	}

	if (year < 1996 || year > 2100) {
	    message(MESS_ERROR, "bad year %d for file %s in state file %s\n",
			year, buf2, stateFilename);
	    fclose(f);
	    return 1;
	}

	if (month < 1 || month > 12) {
	    message(MESS_ERROR, "bad month %d for file %s in state file %s\n",
			month, buf2, stateFilename);
	    fclose(f);
	    return 1;
	}

	if (day < 1 || day > 30) {
	    message(MESS_ERROR, "bad day %d for file %s in state file %s\n",
			day, buf2, stateFilename);
	    fclose(f);
	    return 1;
	}

	year -= 1900, month -= 1;

	st = findState(buf2, statesPtr, numStatesPtr);

	st->lastRotated.tm_mon = month;
	st->lastRotated.tm_mday = day;
	st->lastRotated.tm_year = year;
    }

    fclose(f);

    return 0;

}

void usage(void) {
    fprintf(stderr, "logrotate " VERSION 
		" - Copyright (C) 1995 - Red Hat Software\n");
    fprintf(stderr, "This may be freely redistributed under the terms of "
		"the GNU Public License\n\n");
    fprintf(stderr, "usage: logrotate [-dv] [-s|--state <file>] <config_file>+\n");
    exit(1);
}

int main(int argc, char ** argv) {
    logInfo defConfig = { NULL, ROT_SIZE, 1024 * 1024, 0, 0, NULL, 
			  NULL, NULL, 0 };
    int numLogs = 0, numStates = 0;
    logInfo * logs = NULL;
    logState * states = NULL;
    char * stateFile = STATEFILE;
    int i;
    int rc;
    int arg, long_index;
    struct option options[] = {
	{ "debug", 0, 0, 'd' },
	{ "state", 1, 0, 's' },
	{ "verbose", 0, 0, 'v' },
	{ 0, 0, 0, 0 } 
    };

    logSetLevel(MESS_NORMAL);

    while (1) {
        arg = getopt_long(argc, argv, "ds:v", options, &long_index);
        if (arg == -1) break;

        switch (arg) {
	  case 'd':
	    debug = 1;
	    /* fallthrough */
	  case 'v':
	    logSetLevel(MESS_DEBUG);
	    break;

	  case 's':
	    stateFile = optarg;
	    break;

          case '?':
	    usage();
            break;
	}
    }

    if (optind == argc) {
	usage();
    }

    while (optind < argc) {
	if (readConfigPath(argv[optind], &defConfig, &logs, &numLogs)) {
	    exit(1);
	}
	optind++;
    }

    if (readState(stateFile, &states, &numStates)) {
	exit(1);
    }

    message(MESS_DEBUG, "Handling %d logs\n", numLogs);

    for (i = 0; i < numLogs; i++) {
	rc |= rotateLog(logs + i, &states, &numStates);
    }

    if (!debug && writeState(stateFile, states, numStates)) {
	exit(1);
    }

    return (rc != 0);
}
