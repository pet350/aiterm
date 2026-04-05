#!/bin/bash

if [ ${#OUT}	-eq 0 ]; then declare -x OUT="/dev/stdout";	fi
if [ ${#ERR}	-eq 0 ]; then declare -x ERR="/dev/stdout";	fi

function SHOW_FILENAME()
{
    echo -e '*********************************'
    echo -e '** ' "$1\n"
}

for X in $(ls -1 *.{c,h} Makefile); do
   SHOW_FILENAME $X	| tee -a $ERR $@
   cat $X		| tee -a $OUT $@
   echo -e "\n\n"	| tee -a $OUT $@
done

exit 0
