#!/bin/sh

. ./test-common.sh

cleanup 122

# ------------------------------- Test 122 -----------------------------------
# With "rotate -1" only existing rotated logs are shifted, so a huge index
# does not result in a long running loop over all smaller indices.  Gaps
# between indices are preserved and logs hit by maxage are removed.
preptest test.log 122 1

echo third > test.log.3
echo old > test.log.5
touch -t 200001010000 test.log.5
echo huge > test.log.2000000000

$RLR test-config.122 --force || exit 23

checkoutput <<EOF2
test.log 0
test.log.1 0 zero
test.log.2 0 first
test.log.4 0 third
test.log.2000000001 0 huge
EOF2

for f in test.log.3 test.log.5 test.log.6 test.log.2000000000; do
    if [ -e "$f" ]; then
        echo "unexpected file $f" >&2
        exit 3
    fi
done
