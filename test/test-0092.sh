#!/bin/sh

. ./test-common.sh

# check state file locking
cleanup 92

preptest test.log 92 1

touch state
chmod 0644 state
flock state -c "sleep 10" &

$RLR -f test-config.92 || exit 23

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
