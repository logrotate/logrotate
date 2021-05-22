#!/bin/bash

. ./test-common.sh

# check rotation in copyreduce mode
cleanup 89

preptest test.log 89 0

# skip if logrotate doesn't support copyreduce
$RLR -d test-config.89 2>&1 | grep -q "unsupported option 'copyreduce'" && exit 77
rm -f test.log*

date=$(date -R)
i=0
while [ "$i" -lt 1500 ]; do
	i=`expr "$i" + 1`
	echo "$date: log no. $i"
        # run logrotate during generating logs
	[ "$i" -eq 500 -o "$i" -eq 1000 ] && $RLR test-config.89 --force > /dev/null 2>&1 &
done > test.log

lines=$(LC_ALL=C wc -l test.log* | awk '/total/ { print $1 }')

[ -z "$lines" ] && exit 1
[ "$lines" -ne 1500 ] && exit 1
exit 0
