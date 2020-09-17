#!/bin/sh

. ./test-common.sh

. ./test-common-acl.sh
if [ $ACL_TESTS = 0 ]; then
  echo "Skipping test 32: no ACL support"
  exit 77
fi

cleanup 32

# ------------------------------- Test 32 ------------------------------------
# Without mode in 'create' directive, ACLs should be respected.
# Also check that chmod is respected when setting ACLs
preptest test.log 32 1 0

chmod 600 test.log
setfacl -m u:nobody:rwx test.log

$RLR test-config.32 --force
getfacl test.log|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "test.log must have user:nobody:rwx ACL"
	getfacl test.log
	exit 3
fi

getfacl test.log|grep "group::---" >/dev/null
if [ $? != 0 ]; then
	echo "test.log must have group::--- ACL"
	getfacl test.log
	exit 3
fi

getfacl test.log.1|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "test.log.1 must have user:nobody:rwx ACL"
	getfacl test.log.1
	exit 3
fi

getfacl test.log.1|grep "group::---" >/dev/null
if [ $? != 0 ]; then
	echo "test.log.1 must have group::--- ACL"
	getfacl test.log.1
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
