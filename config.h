/*
 * OS-specific definitions
 */

#ifdef __hpux
    #define DEFAULT_MAIL_COMMAND "/usr/bin/mailx -s"
#endif


/*
 * Default settings for Linux - leave these last.
 */
#ifndef DEFAULT_MAIL_COMMAND
    #define DEFAULT_MAIL_COMMAND "/bin/mail -s"
#endif

#ifndef COMPRESS_COMMAND
    #define COMPRESS_COMMAND "/bin/gzip"
#endif

#ifndef COMPRESS_OPTIONS
    #define COMPRESS_OPTIONS ""
#endif

#ifndef COMPRESS_EXT
    #define COMPRESS_EXT ".gz"
#endif

#ifndef UNCOMPRESS_COMMAND
    #define UNCOMPRESS_COMMAND "/bin/gunzip"
#endif

#ifndef STATEFILE
    #define STATEFILE "/var/lib/logrotate/status"
#endif

