#!/bin/sh

. ./test-common.sh

. ./test-common-acl.sh
if [ $ACL_TESTS = 0 ]; then
  echo "Skipping test 35: no ACL support"
  exit 77
fi

cleanup 35

# ------------------------------- Test 35 ------------------------------------
# Test 'create' directive without mode but with user/group with ACLs. ACLs should
# be respected.
preptest test.log 35 1 0

setfacl -m u:nobody:rwx test.log
$RLR test-config.35 --force

getfacl test.log|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "test.log must contain user:nobody:rwx"
	getfacl test.log
	exit 3
fi

getfacl test.log.1|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "test.log.1 must contain user:nobody:rwx"
	getfacl test.log.1
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
