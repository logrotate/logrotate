#!/bin/sh

. ./test-common.sh

cleanup 114

# ------------------------------- Test 114 ------------------------------------
# A literal '*' in a log file's name (e.g. an nginx access log for a wildcard
# vhost, "*.example.com.access.log") must not be reinterpreted as a glob
# wildcard when logrotate looks for that log's own previously rotated files.
# Otherwise, rotating that log can corrupt the retention of unrelated logs
# that merely happen to share a filename suffix.
genconfig 114

rm -f -- *.example.com.access.log*

days_ago() {
    n=$1
    if date -v -1d > /dev/null 2>&1; then
        date -v-${n}d "+%Y%m%d"
    else
        date "+%Y%m%d" --date "$n day ago"
    fi
}

TODAY=$(/bin/date +%Y%m%d)

# Two sibling logs, each with 5 days of pre-existing dateext rotations. A
# correct "rotate 3" pass over each of them independently should reduce
# that to 3 files: today's new rotation plus the 2 most recent old ones.
for host in a b; do
    echo live > "${host}.example.com.access.log"
    for n in 5 4 3 2 1; do
        echo old > "${host}.example.com.access.log-$(days_ago $n)"
    done
done

# The wildcard-vhost log itself: only 2 days of pre-existing history, so its
# own correct prune has nothing to remove.
echo live > "*.example.com.access.log"
for n in 2 1; do
    echo old > "*.example.com.access.log-$(days_ago $n)"
done

$RLR test-config.114 --force || exit 23

for host in a b; do
    for n in 2 1; do
        f="${host}.example.com.access.log-$(days_ago $n)"
        if [ ! -f "$f" ]; then
            echo "BUG: $f was removed -- rotating the '*' log corrupted" \
                 "'${host}.example.com.access.log' retention" >&2
            exit 2
        fi
    done
    if [ ! -f "${host}.example.com.access.log-$TODAY" ]; then
        echo "expected ${host}.example.com.access.log-$TODAY to exist" >&2
        exit 2
    fi
done

if [ ! -f "*.example.com.access.log-$TODAY" ]; then
    echo "expected *.example.com.access.log-$TODAY to exist" >&2
    exit 2
fi
