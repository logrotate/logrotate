#!/bin/sh

. ./test-common.sh

# we don't want any stuff left from previous runs
cleanup 1

# ------------------------------- Test 1 -------------------------------------
# Without a log file, no rotations should occur
preptest test.log 1 2
$RLR test-config.1

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
EOF

# Put in place a state file that will force a rotation
cat > state <<EOF
logrotate state -- version 1
"$PWD/test.log" 2000-1-1
EOF

# Now force the rotation
$RLR test-config.1
checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2 0 first
EOF

# rerun it to make sure nothing happens
$RLR test-config.1

checkoutput <<EOF
test.log
test.log.1 0 zero
test.log.2 0 first
EOF
