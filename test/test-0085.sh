#!/bin/sh

. ./test-common.sh

cleanup 85

# ------------------------------- Test 85 ------------------------------------
preptest test.log 85 0

$RLR test-config.85 -f

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF

# Set log modification time to some date in the past
touch -t 200001010000 test.log.1

echo "content" >> test.log

$RLR test-config.85 -f

# maxage should remove this file
if [ -f test.log.2 ]; then
    echo "log not removed" >&2
    exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 content
EOF
