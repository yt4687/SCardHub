#! /bin/sh

[ "$1" ] || exit 1
in_name=$1
rc_name=`basename $1 .in`.rc
[ -r "$in_name" ] || exit 1

VERSION=`hg log -r . --template "{date(date, '%Y,%m,%d,%H%M')}" 2> /dev/null || true`
if [ ! "$VERSION" ]; then
	VERSION=`date +%Y,%-m,%-d,%-H%M`
fi

VERSION=`echo $VERSION | sed -e s/,0/,/g`

HGVersion=`hg log -r . --template 'r{rev}:{node|short}' 2> /dev/null || true`
if [ ! "$HGVersion" ]; then
	HGVersion=`echo $VERSION | sed s/,/./g`
fi

sed -e s/@VERSION@/$VERSION/g -e s/@HGVersion@/$HGVersion/g $in_name > $rc_name
