#! /bin/sh
target=autohintexe
curdir=`pwd`

cd debug
make $1
cd $curdir

cd release
make $1
cd $curdir
