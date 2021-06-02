#!/bin/sh

. ./test-common.sh

# check rotation into a directory given as an absolute  pathname
cleanup 13

# ------------------------------- Test 13 ------------------------------------
preptest test.log 13 1 0
rm -rf testdir

$RLR test-config.13 --force || exit 23

ls -l|grep testdir|grep "drwx------." 2>/dev/null >/dev/null
if [ $? != 0 ]; then
	echo "testdir should have mode 2700, but it has:"
	ls -l|grep testdir
	exit 3
fi

checkoutput <<EOF
test.log 0
testdir/test.log.1 0 zero
EOF

rm -rf testdir
mkdir testdir
chmod 777 testdir

echo first >> test.log

$RLR test-config.13 --force || exit 23

ls -l|grep testdir|grep "drwxrwxrwx." 2>/dev/null >/dev/null
if [ $? != 0 ]; then
	echo "testdir should have mode 2777, but it has:"
	ls -l|grep testdir
	exit 3
fi

checkoutput <<EOF
test.log 0
testdir/test.log.1 0 first
EOF

rm -rf testdir
