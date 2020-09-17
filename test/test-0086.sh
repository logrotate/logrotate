#!/bin/sh

. ./test-common.sh

cleanup 86

# ------------------------------- Test 86 ------------------------------------
preptest test.log 86 0

$RLR test-config.86 -f

checkoutput <<EOF
test.log 0
test.log-$(date +%Y%m%d) 0 zero
EOF

LOGFILE="test.log-$(date +%Y%m%d)"

# Set log modification time to some date in the past
touch -t 200001010000 $LOGFILE
mv $LOGFILE test.log-20000101

echo "content" >> test.log

$RLR test-config.86 -f

# maxage should remove this file
if [ -f test.log-20000101 ]; then
    echo "log not removed" >&2
    exit 3
fi

checkoutput <<EOF
test.log 0
test.log-$(date +%Y%m%d) 0 content
EOF
