#!/bin/sh
autoreconf --force --install --warnings=all
ret=$?
if [ $ret -ne 0 ]; then
    echo "autoreconf: failed with return code: $ret"
    exit $ret
fi
echo "The logrotate build system is now prepared.  To build here, run:"
echo "  ./configure && make"
exit 0

# Local Variables:
# mode: sh
# tab-width: 8
# sh-basic-offset: 4
# sh-indentation: 4
# indent-tabs-mode: t
# End:
# ex: shiftwidth=4 tabstop=8
