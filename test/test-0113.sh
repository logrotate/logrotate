#!/bin/sh

. ./test-common.sh

# we don't want any stuff left from previous runs
cleanup 113
rm -f target_state state

# ------------------------------- Test 113 ------------------------------------
# use symlink target of state file
preptest test.log 113 2

touch target_state
ln -s target_state state

if [ ! -L state ]; then
    echo "file 'state' is not a symlink"
    exit 3
fi

if [ ! -f target_state ]; then
    echo "file 'target_state' is not a regular file"
    exit 3
fi

$RLR --force test-config.113 || exit 23

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2 0 first
EOF

if [ ! -L state ]; then
    echo "file 'state' is not a symlink"
    exit 3
fi

if [ ! -f target_state ]; then
    echo "file 'target_state' is not a regular file"
    exit 3
fi
