TEST_CASES = \
	test-0001.sh \
	test-0002.sh \
	test-0003.sh \
	test-0004.sh \
	test-0005.sh \
	test-0006.sh \
	test-0007.sh \
	test-0008.sh \
	test-0009.sh \
	test-0010.sh \
	test-0011.sh \
	test-0012.sh \
	test-0013.sh \
	test-0014.sh \
	test-0015.sh \
	test-0016.sh \
	test-0017.sh \
	test-0018.sh \
	test-0019.sh \
	test-0020.sh \
	test-0021.sh \
	test-0022.sh \
	test-0023.sh \
	test-0024.sh \
	test-0025.sh \
	test-0026.sh \
	test-0027.sh \
	test-0028.sh \
	test-0029.sh \
	test-0030.sh \
	test-0031.sh \
	test-0032.sh \
	test-0033.sh \
	test-0034.sh \
	test-0035.sh \
	test-0036.sh \
	test-0037.sh \
	test-0038.sh \
	test-0039.sh \
	test-0040.sh \
	test-0041.sh \
	test-0042.sh \
	test-0043.sh \
	test-0044.sh \
	test-0045.sh \
	test-0046.sh \
	test-0047.sh \
	test-0048.sh \
	test-0049.sh \
	test-0050.sh \
	test-0051.sh \
	test-0052.sh \
	test-0053.sh \
	test-0054.sh \
	test-0055.sh \
	test-0056.sh \
	test-0057.sh \
	test-0058.sh \
	test-0059.sh \
	test-0060.sh \
	test-0061.sh \
	test-0062.sh \
	test-0063.sh \
	test-0064.sh \
	test-0065.sh \
	test-0066.sh \
	test-0067.sh \
	test-0068.sh \
	test-0069.sh \
	test-0070.sh \
	test-0071.sh \
	test-0072.sh \
	test-0073.sh \
	test-0074.sh \
	test-0075.sh \
	test-0076.sh \
	test-0077.sh \
	test-0078.sh \
	test-0079.sh \
	test-0080.sh \
	test-0081.sh \
	test-0082.sh \
	test-0083.sh \
	test-0084.sh \
	test-0085.sh \
	test-0086.sh \
	test-0087.sh \
	test-0088.sh \
	test-0100.sh \
	test-0101.sh

EXTRA_DIST = \
	compress \
	compress-error \
	mailer \
	test-common.sh \
	test-common-acl.sh \
	test-common-selinux.sh \
	$(TEST_CASES) \
	$(top_srcdir)/test/test-config.*.in

DISTCLEANFILES = \
	test.ACL \
	test.SELINUX \
	test.example

distclean-local:
	rm -rf test-????/

# this needs to run exactly once even if tests run in parallel
all:
	test $(top_srcdir) = $(top_builddir) || ln -fs $(top_srcdir)/test/* .

TESTS_ENVIRONMENT = \
	export LOGROTATE=$(top_builddir)/logrotate ;

TESTS = $(TEST_CASES)
