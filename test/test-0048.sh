#!/bin/sh

. ./test-common.sh

cleanup 48

. ./test-common-acl.sh
if [ $ACL_TESTS = 0 ]; then
  echo "Skipping test 48: no ACL support"
  exit 77
fi

# ------------------------------- Test 48 ------------------------------------
# Test that state file keeps the set ACLs
preptest test.log 48 1 0

cat > state << EOF
logrotate state -- version 2
EOF

setfacl -m u:nobody:rwx state

$RLR test-config.48

getfacl state|grep "user:nobody:rwx" >/dev/null
if [ $? != 0 ]; then
	echo "state file must have acls user:nobody:rwx"
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
