#!/bin/bash

## part of aiterm project
## mkpkg.sh
## creates .tar.gz archive of source code
## By: Peter Talbott
## July 2026

# Function gets the project name from the path
# Path must conform to: /some/location/on/drive/Project/version/
# it will return: Project
# I left the remarked out printf lines to ilustrate how function works
function GET_NAME_FROM_PATH()
{
    PREFIX=$(pwd)
    SECOND=0
    LAST=0
    TOTAL=${#PREFIX}
    COUNT=-1
    while [ $COUNT -lt $TOTAL ]; do
      ((COUNT++))
      #printf "Char %s of %s: " $COUNT $TOTAL
      TEMP=${PREFIX:$COUNT:1}
      #printf "%s " $TEMP
      case $TEMP in
        '/')
          #printf "Match"
          if [ $COUNT -gt $LAST ]; then
              SECOND=$LAST
              LAST=$COUNT
          fi
          ;;
      esac
      #printf "\n"
    done
    #printf "LAST 2 positions %s and %s, in between those: " $SECOND $LAST
    LEN=$((LAST-SECOND))
    NAME=${PREFIX:$((SECOND+1)):$((LEN-1))}
    echo $NAME
}

function GET_VERSION_FROM_PATH()
{
    PREFIX=$(pwd)
    echo ${PREFIX##*/}
}


PROJECT=$(GET_NAME_FROM_PATH)
VERSION=$(GET_VERSION_FROM_PATH)
ARCHIVE="$PROJECT-$VERSION.tar.gz"
LIST="$(ls -1 *.{c,h}) Makefile* README.md LICENSE *.example aiterm-icon.png *.py *.sh"

if [ -f $ARCHIVE ]; then
    rm -v $ARCHIVE
    echo -e "\n"
fi

echo -e "Creating Archive: $ARCHIVE"
tar --gzip -cvf $ARCHIVE -C $(pwd) $LIST
echo -e "Done!\n\n"


