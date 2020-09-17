#!/bin/sh

. ./test-common.sh

. ./test-common-acl.sh
if [ $ACL_TESTS = 0 ]; then
  echo "Skipping test 33: no ACL support"
  exit 77
fi

cleanup 33

# ------------------------------- Test 33 ------------------------------------
# With mode in 'create' directive, ACLs are overwritten by chmod
preptest test.log 33 1 0


setfacl -m u:nobody:rwx test.log
$RLR test-config.33 --force

getfacl test.log|grep "user:nobody:rwx" >/dev/null
if [ $? = 0 ]; then
	echo "test.log must not contain user:nobody:rwx"
	exit 3
fi

getfacl test.log.1|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "test.log.1 must contain user:nobody:rwx"
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
