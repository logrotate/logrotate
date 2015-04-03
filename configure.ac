AC_INIT([logrotate],[3.9.1])

AM_INIT_AUTOMAKE
AC_DEFINE(_GNU_SOURCE)

AM_EXTRA_RECURSIVE_TARGETS([test])

AC_PROG_CC
AC_PROG_CC_STDC
AC_STRUCT_ST_BLKSIZE
AC_STRUCT_ST_BLOCKS

AC_CHECK_LIB([popt],[poptParseArgvString],,
  AC_MSG_ERROR([libpopt required but not found]))

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
  echo "1" > ./test/test.SELINUX;, echo "0" > ./test/test.SELINUX;)

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
  echo "1" > ./test/test.ACL;, echo "0" > ./test/test.ACL;)


AC_CHECK_FUNCS([strptime qsort fork vfork madvise])

AC_CONFIG_FILES([Makefile test/Makefile])
AC_OUTPUT
