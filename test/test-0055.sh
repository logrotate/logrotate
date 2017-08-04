#!/bin/bash

. ./test-common.sh

cleanup 55

# ------------------------------- Test 55 ------------------------------------
# removing last log file when using %s and hourly
rm -f *test.log*
preptest test.log 55 1 0

DATE=""
for i in $(seq 1 60)
do
    DATE=$(/bin/date "+%s" --date "$i hour ago" 2>/dev/null)   
    echo "x" > test.log-$DATE.gz
done

$RLR test-config.55 --force

if [ -e test.log-$DATE.gz ]; then
    echo "File test.log-$DATE.gz should not exist (it should be deleted)"
    exit 3
fi

rm -f *test.log*
