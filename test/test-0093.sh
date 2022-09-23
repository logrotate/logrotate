#!/bin/sh

. ./test-common.sh

# check state file locking with --wait-for-state-lock
cleanup 93

preptest test.log 93 1

# the locking in logrotate does not reliably work in case the state file
# is not created in advance
touch state
chmod 0640 state

# run two instances of logrotate in parallel
$RLR -f --wait-for-state-lock test-config.93 &
sleep 1
$RLR -f --wait-for-state-lock test-config.93
EC2=$?
wait || exit $?
[ $EC2 -eq 0 ] || exit $EC2

checkoutput <<EOF
test.log 0
test.log.1 0
test.log.2 0 zero
EOF
