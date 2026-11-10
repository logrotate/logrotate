#!/bin/sh

. ./test-common.sh

cleanup 127

# ------------------------------- Test 127 ------------------------------------
# Glob metacharacters in a log file name must not make the search for old
# dateext rotations match the rotations of other logs. The bracket expression
# is longer than the date suffix, so a date parsed at an offset computed from
# the unescaped name would lie beyond the end of the other logs' rotations
# (an out-of-bounds read, detectable with valgrind).
preptest test.log 127 0

NAME='test[abcdefghijklmnopqrstuvwxyz]'

echo own > "$NAME.log"
echo old > "$NAME.log-20000101"
echo foo > testa.log-20000102
echo bar > testb.log-20000103

$RLR test-config.127 --force || exit 23

DATESTRING=$(/bin/date +%Y%m%d)

for f in "$NAME.log" "$NAME.log-$DATESTRING" testa.log-20000102 testb.log-20000103; do
	if [ ! -e "$f" ]; then
		echo "$f is missing"
		exit 3
	fi
done

if [ -e "$NAME.log-20000101" ]; then
	echo "old rotation $NAME.log-20000101 was not removed"
	exit 3
fi

if [ "$(cat testa.log-20000102)" != foo ] || [ "$(cat testb.log-20000103)" != bar ]; then
	echo "rotations of other logs were modified"
	exit 3
fi
