#! /bin/sh

set -e

mkdir -p bin/x64

make clean all X86=1
mv -fv WinSCard.dll bin

make clean all
mv -fv WinSCard.dll bin/x64

make clean

if [ "$1" = dist ]; then
	cp -prv bin dist
	cp -pv *.md *.ini dist
	z=$(hg log -r . --template 'SCardHub-binary-r{rev}-{node|short}.7z')
	cd dist

	sha1sum `find * -type f` > sha1sums.txt
	gpg --local-user "John Doe 2015" --armor --detach-sign sha1sums.txt

	7z a ../$z .
	cd ..
	rm -fr dist
fi
