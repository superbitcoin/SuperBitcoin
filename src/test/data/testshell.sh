#!/bin/sh
echo  "this is shell!hello sbtc test!"

workdir=$(cd $(dirname $0); pwd)
echo "$workdir"


for filename in `ls  $workdir/*.json`
do


if [ -f "$filename.h" ]; then
   echo "File exists!"
else
   touch $filename.h
fi
fileshortname=${filename##*/}

{
	 echo "namespace json_tests{" && \
	 echo "static unsigned const char ${fileshortname%%.*}[] = {" && \
	 hexdump -v -e '8/1 "0x%02x, "' -e '"\n"'  $filename && \
	 echo "};};"; \

}>$filename.h
sed -i 's/0x  ,//g'  $filename.h

done

