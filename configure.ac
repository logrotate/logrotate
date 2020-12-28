AC_INIT([logrotate],m4_esyscmd([build-aux/git-version-gen --prefix '' .tarball-version]),
[],[],[https://github.com/logrotate/logrotate])

dnl foreign: Do not require AUTHORS, ChangeLog, NEWS, and README to exist
dnl dist-xz: Produce tar.xz version of the release archive
AM_INIT_AUTOMAKE(foreign dist-xz)

# --enable-silent-rules
m4_ifdef([AM_SILENT_RULES],
  [AM_SILENT_RULES([yes])],
  [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS

AC_CANONICAL_HOST

dnl Use 64-bit file offsets on 32-bit systems (defines C macros if necessary)
AC_SYS_LARGEFILE

dnl needed for basename() on OS X
AC_CHECK_HEADERS([libgen.h])

AC_CHECK_LIB([popt],[poptParseArgvString],,
  AC_MSG_ERROR([libpopt required but not found]))

dnl Needed for out-of-source builds
mkdir -p test

WITH_SELINUX="no"
AC_ARG_WITH([selinux],
  [AS_HELP_STRING([--with-selinux],
    [support handling SELinux contexts (yes,no,check) @<:@default=check@:>@])],
  [],
  [with_selinux=check])
AS_CASE(["$with_selinux"],
  [yes], [AC_CHECK_LIB([selinux],[getfscreatecon_raw])],
  [no], [],
  [AC_CHECK_LIB([selinux],[getfscreatecon_raw])])
AS_IF([test "$ac_cv_lib_selinux_getfscreatecon_raw" = yes],
  echo "1" > ./test/test.SELINUX; WITH_SELINUX="yes";, echo "0" > ./test/test.SELINUX;)

WITH_ACL="no"
AC_ARG_WITH([acl],
  [AS_HELP_STRING([--with-acl],
    [support handling ACL (yes,no,check) @<:@default=check@:>@])],
  [],
  [with_acl=check])
AS_CASE(["$with_acl"],
  [yes], [AC_CHECK_LIB([acl],[acl_get_file])],
  [no], [],
  [AC_CHECK_LIB([acl],[acl_get_file])])
AS_IF([test "$ac_cv_lib_acl_acl_get_file" = yes],
  echo "1" > ./test/test.ACL; WITH_ACL="yes";, echo "0" > ./test/test.ACL;)

DEFAULT_MAIL_COMMAND="/bin/mail"
COMPRESS_COMMAND="/bin/gzip"
UNCOMPRESS_COMMAND="/bin/gunzip"
COMPRESS_EXT=".gz"
STATE_FILE_PATH="/var/lib/logrotate.status"

case "$host" in
  *-hpux*)
          DEFAULT_MAIL_COMMAND="/usr/bin/mailx"
          COMPRESS_COMMAND="/usr/contrib/bin/gzip"
          UNCOMPRESS_COMMAND="/usr/contrib/bin/gunzip"
          ;;
  *-solaris*|*-sunos*)
          DEFAULT_MAIL_COMMAND="/usr/bin/mailx"
          ;;
  *-netbsd*)
          DEFAULT_MAIL_COMMAND="/usr/bin/mail"
          COMPRESS_COMMAND="/usr/bin/gzip"
          UNCOMPRESS_COMMAND="/usr/bin/gunzip"
          ;;
  *-darwin*)
          COMPRESS_COMMAND="/usr/bin/gzip"
          UNCOMPRESS_COMMAND="/usr/bin/gunzip"
          ;;
esac

AC_ARG_WITH([state-file-path],
  AS_HELP_STRING([--with-state-file-path=PATH],
                 [path to state file (/var/lib/logrotate.status by default)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-state-file-path=PATH requires a path to be specified])
        ;;
      *)
        STATE_FILE_PATH="$withval"
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([STATEFILE], ["$STATE_FILE_PATH"], [State file path.])
AC_SUBST(STATE_FILE_PATH)

AC_ARG_WITH([default-mail-command],
  AS_HELP_STRING([--with-default-mail-command=COMMAND],
                 [default mail command (e.g. /bin/mail)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-default-mail-command=COMMAND requires a command to be specified])
        ;;
      *)
        DEFAULT_MAIL_COMMAND="$withval"
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([DEFAULT_MAIL_COMMAND], ["$DEFAULT_MAIL_COMMAND"], [default mail command])
AC_SUBST(DEFAULT_MAIL_COMMAND)

AC_ARG_WITH([compress-command],
  AS_HELP_STRING([--with-compress-command=COMMAND],
                 [compress command (default: /bin/gzip)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-compress-command=COMMAND requires a command to be specified])
        ;;
      *)
        COMPRESS_COMMAND="$withval"
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([COMPRESS_COMMAND], "$COMPRESS_COMMAND", [default compress command])
AC_SUBST(COMPRESS_COMMAND)

AC_ARG_WITH([uncompress-command],
  AS_HELP_STRING([--with-uncompress-command=COMMAND],
                 [uncompress command (default: /bin/gunzip)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-uncompress-command=COMMAND requires a command to be specified])
        ;;
      *)
        UNCOMPRESS_COMMAND="$withval"
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([UNCOMPRESS_COMMAND], "$UNCOMPRESS_COMMAND", [default uncompress command])
AC_SUBST(UNCOMPRESS_COMMAND)

AC_ARG_WITH([compress-extension],
  AS_HELP_STRING([--with-compress-extension=EXTENSION],
                 [compress extension (default: .gz)]),
  [
    case "$withval" in
      .*)
        COMPRESS_EXT="$withval"
        ;;
      *)
        AC_MSG_ERROR([--with-compress-extension=EXTENSION must start with .])
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([COMPRESS_EXT], ["$COMPRESS_EXT"], [default compress extension])
AC_SUBST(COMPRESS_EXT)

AC_DEFINE_UNQUOTED([ROOT_UID], [0], [Root user-id.])
AC_SUBST(ROOT_UID)

AC_CHECK_FUNCS([asprintf madvise qsort secure_getenv strndup strptime utimensat vsyslog])
AC_CONFIG_HEADERS([config.h])

AM_CFLAGS="\
 -Wall\
 -Wconversion\
 -Wdeclaration-after-statement\
 -Wextra\
 -Wmissing-format-attribute\
 -Wmissing-noreturn\
 -Wmissing-prototypes\
 -Wpointer-arith\
 -Wshadow\
 -Wstrict-prototypes\
 -Wundef\
 -Wunused\
 -Wwrite-strings"

AC_ARG_ENABLE([werror],
        [AS_HELP_STRING([--enable-werror],
                [Treat warnings as errors (default: warnings are not errors)])],
                [enable_werror="$enableval"],
                [enable_werror=no])
AS_IF([test "x$enable_werror" = "xyes"], [AM_CFLAGS="$AM_CFLAGS -Werror"])
AC_SUBST([AM_CFLAGS])

AC_CONFIG_FILES([Makefile test/Makefile logrotate.8 logrotate.spec])
AC_OUTPUT

AC_MSG_RESULT([
  ${PACKAGE_NAME} ${VERSION}

  SELinux support:        ${WITH_SELINUX}
  ACL support:            ${WITH_ACL}
  default mail command:   ${DEFAULT_MAIL_COMMAND}
  compress command:       ${COMPRESS_COMMAND}
  uncompress command:     ${UNCOMPRESS_COMMAND}
  compress extension:     ${COMPRESS_EXT}
  statefile path:         ${STATE_FILE_PATH}
])
