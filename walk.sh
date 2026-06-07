#!/bin/bash

GAME=sfrush
GOOD=../afsys/img_sfrush
#MAYBE=here
MAYBE=test
LIST=$GAME.files

if [ ! -d $GOOD ]; then
	echo "No such directory: $GOOD"
	exit 1
fi
if [ ! -d $MAYBE ]; then
	echo "No such directory: $MAYBE"
	exit 1
fi
if [ ! -e $LIST ]; then
	echo "No such file as $LIST"
	exit 1
fi

good=0
bad=0
missing=0
skipped=0
rm -f walk.good walk.bad walk.missing walk.skipped
while read line; do  
	if [ "${line##*.}" = "MAI" ]; then
		echo >> walk.skipped "Skipped $line"
		skipped=$((skipped+1))
		continue;
	fi
	if [ ! -s $GOOD/$line ]; then
		echo >> walk.skipped "Skipped $line due to size being 0"
		skipped=$((skipped+1))
		continue
	fi
	if [ ! -e $MAYBE/$line ]; then
		echo >> walk.missing "File $MAYBE/$line doesn't exist"
		missing=$((missing+1))
		continue;
	fi
	cmp -s $GOOD/$line $MAYBE/$line
	if [ $? -ne 0 ]; then
		echo >> walk.bad "File $GOOD/$line doesn't match $MAYBE/$line"
		bad=$((bad+1))
		continue;
	fi
	echo >> walk.good "File $line is good"
	good=$((good+1))  
done < $LIST
echo "Totals of good: $good, bad: $bad, skipped=$skipped, missing: $missing"
