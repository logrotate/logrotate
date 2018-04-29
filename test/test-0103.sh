#!/bin/bash

. ./test-common.sh

cleanup 103

# ------------------------------- Test 103 -----------------------------------
# size option and the time interval option are mutually exclusive, so the user
# should be warned, but only if they are used inside the same log config
preptest test.log 103 1 0

! $RLR -d test-config.103 2>&1 | \
    grep -q "size option is mutually exclusive with the time interval options."
