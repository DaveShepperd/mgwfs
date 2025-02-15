#!/bin/bash

rm -f walk.bad walk.good walk.missing

cd foo
if [ $? -ne 0 ]; then
	echo "Cannot cd to foo"
	exit 1
fi

LIST=../foo.lst
OTHER=/d3/afsys/img_good
bad=0
good=0
missing=0
while read line; do
        if [ ! -s $OTHER/$line ]; then
                echo >> ../walk.missing "File $line is missing from $OTHER"
                missing=$((missing+1))
                continue
        fi      
        cmp -s $line $OTHER/$line
        if [ $? -ne 0 ]; then
                echo >> ../walk.bad "File $line doesn't match $OTHER/$line"
                bad=$((bad+1))
                continue;
        fi
        echo >> ../walk.good "File $line is good"
        good=$((good+1))
done < $LIST
echo "Totals of good: $good, bad: $bad, skipped=$skipped, missing: $missing"

