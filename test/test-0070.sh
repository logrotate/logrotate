#!/bin/bash

. ./test-common.sh

cleanup 70

# ------------------------------- Test 70 ------------------------------------
# No rotation should occur because file is too young
preptest test.log 70 2

# Put in place a state file that will force a rotation
cat > state <<EOF
logrotate state -- version 2
"$PWD/test.log" 2000-1-1
EOF

$RLR test-config.70

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
EOF
