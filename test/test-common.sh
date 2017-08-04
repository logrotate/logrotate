# common variables and functions for legacy tests
LOGROTATE="$(readlink -f $LOGROTATE)"
RLR="$LOGROTATE -m ./mailer -s state"

TESTDIR="$(basename "$0" .sh)"
mkdir -p "$TESTDIR"
cd "$TESTDIR" || exit $?

TESTNUM="$(printf "%s\n" "$TESTDIR" | sed -e 's/^test-0*//')"

import() {
  [ -e "$1" ] && return
  [ -e "../$1" ] || return
  ln -s "../$1"
}

import "compress"
import "compress-error"
import "mailer"
import "test-common-acl.sh"
import "test-common-selinux.sh"
import "test-config.$TESTNUM.in"

cleanup() {
    rm -f test*.log* anothertest*.log* state test-config. scriptout mail-out compress-args compress-env different*.log*
    rm -f $(ls | egrep '^test-config.[0-9]+$')

    [ -n "$1" ] && echo "Running test $1"
    return 0
}

genconfig() {
    input=test-config.$1.in
    output=test-config.$1
    user=$(id -u -n)
    group=$(id -g -n)
    sed "s,&DIR&,$PWD,g" < $input | sed "s,&USER&,$user,g" | sed "s,&GROUP&,$group,g" > $output
    config_crc=$(md5sum $output)
}

createlog() {
    num=$1
    file=$2
    cl_compressed=$3

    case $num in
	0)
	    what=zero
	    ;;
	1)
	    what=first
	    ;;
	2)
	    what=second
	    ;;
	3)
	    what=third
	    ;;
	4)
	    what=fourth
	    ;;
	5)
	    what=fifth
	    ;;
	6)
	    what=sixth
	    ;;
	7)
	    what=seventh
	    ;;
	8)
	    what=eight
	    ;;
	9)
	    what=ninth
	    ;;
	*)
	    exit 1
	    ;;
    esac

    echo $what > $file
    [ -n "$cl_compressed" ] && gzip -9 $file
}

createlogs() {
    base=$1
    numlogs=$2
    cls_compressed=$3

    rm -f ${base}*

    num=0
    while [ $num != $numlogs ]; do
	if [ $num = 0 ]; then
	    createlog 0 $base
	else
	    createlog $num ${base}.$num $cls_compressed
	fi

	num=`expr $num + 1`
    done
}

checkmail() {
    (echo -s $PWD/$1 user@myhost.org; echo $2) | diff -u - mail-out
    if [ $? != 0 ]; then
        exit 5
    fi
}

checkoutput() {
    while read line; do
	set $line
	file=$1
	co_compressed=$2
	if [ "$#" -le 2 ]; then
		shift $#
	else
		shift 2
	fi

	fileother=`echo $line | awk '{print $1}'`
	expected=`echo $line | cut -s -d\  -f3-`

	if [ $file != $fileother ]; then
	    echo "unexpected file $file'" >&2
	    exit 2
	fi

	if [ ! -f $file ]; then
	    echo "file $file does not exist"
	fi

	if [ -n "$co_compressed" ] && [ "$co_compressed" != 0 ]; then
		contents=`gunzip -c $file`
	else
		contents=`cat $file | tr -d '\000'`
	fi
	if [ "$contents" != "$expected" ]; then
	    echo "file $file does not contain expected results (compressed $co_compressed, args $*)" >&2
	    echo contains: \'$contents\'
	    echo expected: \'$expected\'
	    exit 2
	fi
	echo "$config_crc" | md5sum -c - 2>&1 > /dev/null
	if [ $? != 0 ]; then
		echo "config file $output has been altered: MD5 sum mismatch"
		exit 3
	fi
    done
}

preptest() {
    base=$1
    confignum=$2
    numlogs=$3
    pt_compressed=$4

    rm -f $base*
    rm -f state

    genconfig $confignum
    createlogs $base $numlogs $pt_compressed
}
