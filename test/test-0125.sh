#!/bin/sh

. ./test-common.sh

cleanup 125

# ------------------------------- Test 125 -----------------------------------
# The minage value is limited, so it can be converted into seconds as int
# without overflow.
preptest test.log 125 0
createlog 0 test2.log

# Put in place a state with an old rotation date
cat > state <<EOF2
logrotate state -- version 2
"$PWD/test.log" $(($(date "+%Y") - 10))-1-1
EOF2

$RLR test-config.125 2>error.log && exit 23

grep "bad minimum age '24856'" error.log >/dev/null
if [ $? != 0 ]; then
    echo "No error printed, but there should be one."
    exit 3
fi
if grep "bad minimum age '24855'" error.log >/dev/null; then
    echo "The largest valid minage value was rejected."
    exit 3
fi

rm error.log

checkoutput <<EOF2
test.log 0 zero
test2.log 0 zero
EOF2

if [ -e test.log.1 ] || [ -e test2.log.1 ]; then
    echo "log rotated despite minage" >&2
    exit 3
fi
