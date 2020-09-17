#!/bin/sh

. ./test-common.sh

cleanup 6

# ------------------------------- Test 6 -------------------------------------
preptest test.log 6 1
preptest anothertest.log 6 1
. ./test-common-selinux.sh
if [ $SELINUX_TESTS = 1 ]; then
	chcon --type=logrotate_tmp_t test.log
else
	echo "Skipping SELinux part of test 6"
fi
$RLR test-config.6 --force

if [ $SELINUX_TESTS = 1 ]; then
	ls -Z test.log.0|grep logrotate_tmp_t >/dev/null
	if [ $? != 0 ]; then
		echo "test.log.0 should have selinux context logrotate_tmp_t."
		exit 3
	fi

	ls -Z anothertest.log.0|grep logrotate_tmp_t >/dev/null
	if [ $? = 0 ]; then
		echo "anothertest.log.0 should not have selinux context logrotate_tmp_t."
		exit 3
	fi
fi

checkoutput <<EOF
test.log 0
test.log.0 0 zero
anothertest.log 0
anothertest.log.0 0 zero
scriptout 0 foo
EOF
