#!/bin/sh

. ./test-common.sh

cleanup 81

# ------------------------------- Test 81 ------------------------------------
# size option and the time interval option are mutually exclusive, so the user
# should be warned, but only if they are used inside the same log config
preptest test.log 81 1 0

! $RLR -d test-config.81 2>&1 | \
    grep -q "warning: '.*' overrides previously specified '.*'"
