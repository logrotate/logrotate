#!/bin/sh

. ./test-common.sh

cleanup 10

# ------------------------------- Test 10 ------------------------------------
preptest test.log 10 1

. ./test-common-selinux.sh
if [ $SELINUX_TESTS = 1 ]; then
	chcon --type=logrotate_tmp_t test.log
else
	echo "Skipping SELinux part of test 10"
fi

$RLR test-config.10 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF

echo "newfile" > test.log

$RLR test-config.10 --force

if [ $SELINUX_TESTS = 1 ]; then
	ls -Z test.log.2.gz|grep logrotate_tmp_t >/dev/null
	if [ $? != 0 ]; then
		echo "test.log.2.gz should have selinux context logrotate_tmp_t."
		ls -Z test.log.2.gz
		exit 3
	fi

	ls -Z test.log.1|grep logrotate_tmp_t >/dev/null
	if [ $? != 0 ]; then
		echo "test.log.1 should have selinux context logrotate_tmp_t."
		ls -Z test.log.1
		exit 3
	fi
fi

checkoutput <<EOF
test.log 0
test.log.1 0 newfile
test.log.2.gz 1 zero
EOF

checkmail test.log.1 newfile
