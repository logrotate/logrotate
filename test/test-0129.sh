#!/bin/sh

. ./test-common.sh

cleanup 129

# ------------------------------- Test 129 ------------------------------------
# Glob metacharacters in the directory or the base name of a log must not make
# the lookups of its own rotations match files of other logs: dateext pruning
# with a metacharacter in the directory, dateext delayed compression, and the
# search for the last numbered rotation with 'rotate -1'. The live logs
# da/test.log, testa.log and testx.log, matched by the unescaped names, must not
# be rotated.
genconfig 129

rm -rf -- 'd[ab]' da db

mkdir 'd[ab]' da db
echo own > 'd[ab]/test.log'
echo old > 'd[ab]/test.log-20000101'
echo live > da/test.log
echo foo > da/test.log-20000102
echo bar > db/test.log-20000103

echo own > 'test[abc].log'
echo old > 'test[abc].log-20000101'
echo live > testa.log
echo foo > testa.log-20000102

echo own > 'test[xyz].log'
echo first > 'test[xyz].log.1'
echo second > 'test[xyz].log.2'
echo live > testx.log
echo foo > testx.log.1

$RLR test-config.129 --force || exit 23

DATESTRING=$(/bin/date +%Y%m%d)

check_content() {
	if [ "$(cat "$1")" != "$2" ]; then
		echo "$1 does not contain '$2'"
		exit 3
	fi
}

# directory metacharacter: own old rotation pruned, other directories untouched
check_content "d[ab]/test.log-$DATESTRING" own
if [ -e 'd[ab]/test.log-20000101' ]; then
	echo "old rotation d[ab]/test.log-20000101 was not removed"
	exit 3
fi
check_content da/test.log-20000102 foo
check_content db/test.log-20000103 bar

# delayed compression: own previous rotation compressed, other log untouched
check_content "test[abc].log-$DATESTRING" own
if [ "$(gzip -dc 'test[abc].log-20000101.gz')" != old ]; then
	echo "previous rotation test[abc].log-20000101 was not compressed"
	exit 3
fi
check_content testa.log-20000102 foo

# rotate -1: all own numbered rotations shifted and kept, other log untouched
check_content 'test[xyz].log.1' own
check_content 'test[xyz].log.2' first
check_content 'test[xyz].log.3' second
check_content testx.log.1 foo

# live logs of other names not rotated
for f in da/test.log testa.log testx.log; do
	check_content "$f" live
done
if [ -e "da/test.log-$DATESTRING" ] || [ -e "testa.log-$DATESTRING" ] || [ -e testx.log.2 ]; then
	echo "a log not named in the configuration was rotated"
	exit 3
fi

rm -rf -- 'd[ab]' da db
