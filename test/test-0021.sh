#!/bin/sh

. ./test-common.sh

cleanup 21

# ------------------------------- Test 21 ------------------------------------
# different base name, so it should not find the file
preptest differenttest.log 21 1

checkoutput <<EOF
differenttest.log 0 zero
differenttest.log.1 0 first
EOF

$RLR test-config.21 --force 2>error.log || exit 23

checkoutput <<EOF
differenttest.log 0 zero
differenttest.log.1 0 first
EOF

if [ -e test.log ]; then
	echo "test-log exists"
	exit 3
fi

if [ -e test.log.1 ]; then
	echo "test.log.1 exists"
	exit 3
fi
