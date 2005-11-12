/*
 * OS-specific definitions
 */

#ifdef __hpux
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#define COMPRESS_COMMAND "/usr/contrib/bin/gzip"
#define UNCOMPRESS_COMMAND " /usr/contrib/bin/gunzip"
#define STATEFILE "/var/run/logrotate.status"
#endif

#ifdef SunOS
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#define COMPRESS_COMMAND "/usr/local/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/local/bin/gunzip"
#define STATEFILE "/var/log/logrotate.status"
#endif

/*
 * Default settings for Linux - leave these last.
 */
#ifndef DEFAULT_MAIL_COMMAND
#define DEFAULT_MAIL_COMMAND "/bin/mail"
#endif

#ifndef COMPRESS_COMMAND
#define COMPRESS_COMMAND "/bin/gzip"
#endif

#ifndef COMPRESS_EXT
#define COMPRESS_EXT ".gz"
#endif

#ifndef UNCOMPRESS_COMMAND
#define UNCOMPRESS_COMMAND "/bin/gunzip"
#endif

#ifndef STATEFILE
#define STATEFILE "/var/lib/logrotate.status"
#endif
