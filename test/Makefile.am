EXTRA_DIST = \
	compress \
	compress-error \
	mailer \
	test \
	$(top_srcdir)/test/test-config.*.in

DISTCLEANFILES = \
	test.ACL \
	test.SELINUX \
	test.example

check:
	test $(top_srcdir) = $(top_builddir) || ln -fs $(top_srcdir)/test/* .
	LOGROTATE=$(top_builddir)/logrotate ./test
