#!/bin/bash

if [ ${#OUT}	-eq 0 ]; then declare -x OUT="/dev/stdout";	fi
if [ ${#ERR}	-eq 0 ]; then declare -x ERR="/dev/stdout";	fi
if [ ${#WAIT}   -eq 0 ]; then declare -i WAIT=2;		fi
if [ ${#FOOTER}	-eq 0 ]; then declare -i FOOTER=0;		fi

function SHOW_FILENAME()
{
    echo -e "\n"
    echo -e '// ================================='
    echo -e '//'
    echo -e "//  $1"
    echo -e '//'
    echo -e '// ================================='
    echo -e "\n"

}

for X in $(ls -1 *.{c,h} Makefile *.conf); do
   SHOW_FILENAME $X	| tee -a $ERR $@
   if [ $FOOTER -eq 1 ]; then SHOW_FILENAME | tee -a $ERR $@; fi
   cat $X		| tee -a $OUT $@
   echo -e "\n\n"	| tee -a $OUT $@
   sleep $WAIT
done

exit 0
