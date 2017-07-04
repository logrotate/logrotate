#!/bin/bash

. ./test-common.sh

cleanup 34

# ------------------------------- Test 34 ------------------------------------
# We support changing user/mode without setting mode in create directive now
# We can't change user/group as normal user, so this test uses debug mode and
# checks the logrotate -d output.
preptest test.log 34 1 0

$RLR test-config.34 -d -f 2>&1|grep "uid = 0 gid = 0" > /dev/null

if [ $? != 0 ]; then
	echo "logrotate output must contain 'uid = 0 gid = 0'"
	exit 3
fi

checkoutput <<EOF
test.log 0 zero
EOF
