#!/bin/sh

. ./test-common.sh

cleanup 49

# ------------------------------- Test 49 ------------------------------------
# Test that state files without hours/minutes/seconds still works properly
preptest test.log 49 1 0

cat > state << EOF
logrotate state -- version 2
"test.log" 2012-8-19
EOF

$RLR test-config.49

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
