/*
 * OS-specific definitions
 */

#define ROOT_UID 0

#ifdef __hpux
#ifndef DEFAULT_MAIL_COMMAND
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#endif
#define COMPRESS_COMMAND "/usr/contrib/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/contrib/bin/gunzip"
#ifndef STATEFILE
#define STATEFILE "/var/run/logrotate.status"
#endif /* STATEFILE */
#endif

#ifdef SunOS
#ifndef DEFAULT_MAIL_COMMAND
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#endif
#endif

#ifdef __NetBSD__
#ifndef DEFAULT_MAIL_COMMAND
#define DEFAULT_MAIL_COMMAND "/usr/bin/mail -s"
#endif
#define COMPRESS_COMMAND "/usr/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/bin/gunzip"
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define COMPRESS_COMMAND "/usr/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/bin/gunzip"
#endif

/*
 * Default settings (mainly for Linux) - leave these last.
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
