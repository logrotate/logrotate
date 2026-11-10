#!/bin/sh

. ./test-common.sh

cleanup 128

# ------------------------------- Test 128 ------------------------------------
# A literal '*' in a log file name (e.g. an nginx access log for a wildcard
# vhost, "*.example.com.access.log") must not be reinterpreted as a glob
# wildcard when looking for that log's own dateext rotations. Otherwise,
# rotating that log prunes the rotations of unrelated logs sharing the suffix.
genconfig 128

rm -f -- *.example.com.access.log*

# Two sibling logs, each with 5 old rotations.
for host in a b; do
	echo live > "$host.example.com.access.log"
	for d in 20000101 20000102 20000103 20000104 20000105; do
		echo "$host $d" > "$host.example.com.access.log-$d"
	done
done

# The wildcard-vhost log itself, with only 2 old rotations.
echo live > '*.example.com.access.log'
for d in 20000104 20000105; do
	echo "wildcard $d" > "*.example.com.access.log-$d"
done

$RLR test-config.128 --force || exit 23

DATESTRING=$(/bin/date +%Y%m%d)

# With 'rotate 3' each log keeps today's rotation plus its 2 newest old ones.
for f in '*' a b; do
	for d in 20000104 20000105 "$DATESTRING"; do
		if [ ! -f "$f.example.com.access.log-$d" ]; then
			echo "$f.example.com.access.log-$d is missing"
			exit 3
		fi
	done
done

for host in a b; do
	for d in 20000101 20000102 20000103; do
		if [ -e "$host.example.com.access.log-$d" ]; then
			echo "old rotation $host.example.com.access.log-$d was not removed"
			exit 3
		fi
	done
	if [ "$(cat "$host.example.com.access.log-20000105")" != "$host 20000105" ]; then
		echo "rotation $host.example.com.access.log-20000105 was modified"
		exit 3
	fi
done

if [ "$(cat '*.example.com.access.log-20000105')" != "wildcard 20000105" ]; then
	echo "rotation *.example.com.access.log-20000105 was modified"
	exit 3
fi

rm -f -- *.example.com.access.log*
