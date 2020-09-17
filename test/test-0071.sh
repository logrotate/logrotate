#!/bin/sh

. ./test-common.sh

cleanup 71

# ------------------------------- Test 71 ------------------------------------
# Rotation should occur because file is old
preptest test.log 71 2

# Set log modification time to some date in the past
touch -t 200001010000 test.log

# Put in place a state file that will force a rotation
cat > state <<EOF
logrotate state -- version 2
"$PWD/test.log" 2000-1-1
EOF

$RLR test-config.71

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2 0 first
EOF
