#!/bin/sh

. ./test-common.sh

# skip the test if /dev/null is not readable
test -r /dev/null || exit 77

# we don't want any stuff left from previous runs
cleanup 89

# ------------------------------- Test 89 ------------------------------------
# using /dev/null as state file tells logrotate not to write the state file
preptest test.log 89 2
$RLR --state /dev/null test-config.89 || exit 23
