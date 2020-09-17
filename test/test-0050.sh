#!/bin/sh

. ./test-common.sh

cleanup 50

# ------------------------------- Test 50 ------------------------------------
# test that hourly rotation works properly
preptest test.log 50 1 0

DATESTRING=$(/bin/date +%Y%m%d%H)
NOW=$(/bin/date "+%Y-%-m-%-d-%-H" 2>/dev/null)
HOURAGO=$(/bin/date "+%Y-%-m-%-d-%-H" --date "1 hour ago" 2>/dev/null)
GNUDATE=$?

# --force to trigger rotation
$RLR test-config.50 --force
checkoutput <<EOF
test.log 0
test.log-$DATESTRING 0 zero
EOF

# It should not rotate this hour again
echo second > test.log
rm -f test.log-$DATESTRING
$RLR test-config.50
checkoutput <<EOF
test.log 0 second
EOF

if [ -f test.log.1 ]; then
    echo "file $file does exist!"
    exit 2
fi

if [ $GNUDATE = 0 ]; then
# Simulate previous rotation by editing state file. This should overwrite
# our previously rotated log
sed -i "s,$NOW,$HOURAGO,g" state
$RLR test-config.50
checkoutput <<EOF
test.log 0
test.log-$DATESTRING 0 second
EOF
else
echo "Does not have GNU Date, skipping part of this test"
fi
