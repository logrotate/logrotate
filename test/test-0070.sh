#!/bin/sh

. ./test-common.sh

cleanup 70

# ------------------------------- Test 70 ------------------------------------
# No rotation should occur because file is too young
preptest test.log 70 2

# Set log modification time to current date.
# In reprotest (with faketime(1)) environments the logs might not be created
# with the faked system time.
touch -t $(date +%Y%m%d%H%M) test.log

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
test.log.2 0 second
EOF

$RLR test-config.70

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
test.log.2 0 second
EOF

# Put in place a state with an old rotation date
cat > state <<EOF
logrotate state -- version 2
"$PWD/test.log" $(($(date "+%Y") - 10))-1-1
EOF

$RLR test-config.70

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
test.log.2 0 second
EOF
