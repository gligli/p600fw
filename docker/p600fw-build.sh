#!/bin/sh

BRANCH=master
REPO_URL=https://github.com/gligli/p600fw.git
DIR=p600fw

DIST=/dist
FILES="p600firmware.bin p600firmware.hex p600firmware.syx"

set -e
set -x
git clone $REPO_URL $DIR
cd $DIR
if [ "$BRANCH" != "master" ]; then
  git checkout $BRANCH
fi

cd firmware
make

mkdir $DIST
for f in $FILES; do
  cp $f /dist/$f
done
