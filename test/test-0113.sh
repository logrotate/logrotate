#!/bin/sh

. ./test-common.sh

cleanup 113

# ------------------------------- Test 113 ------------------------------------
# test that monthly rotation works properly
preptest test.log 113 1 0

DATESTRING=$(/bin/date +%Y%m%d)
NOW=$(/bin/date "+%Y-%-m-%-d-%-H" 2>/dev/null)
MONTHAGO=$(/bin/date "+%Y-%-m-%-d-%-H" --date "32 days ago" 2>/dev/null)
GNUDATE=$?

# --force to trigger rotation
$RLR test-config.113 --force || exit 23
checkoutput <<EOF
test.log 0
test.log-$DATESTRING 0 zero
EOF

# It should not rotate this hour again
echo second > test.log
rm -f "test.log-$DATESTRING"
$RLR test-config.113 || exit 23
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
sed -i "s,$NOW,$MONTHAGO,g" state
$RLR test-config.113 || exit 23
checkoutput <<EOF
test.log 0
test.log-$DATESTRING 0 second
EOF
else
echo "Does not have GNU Date, skipping part of this test"
fi
