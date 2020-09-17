#!/bin/sh

. ./test-common.sh

cleanup 80

# ------------------------------- Test 80 ------------------------------------
# size option and the time interval option are mutually exclusive, so the user
# should be warned.
preptest test.log 80 1 0

$RLR -d test-config.80 2>&1 | \
    grep -q "warning: 'daily' overrides previously specified 'size'"
