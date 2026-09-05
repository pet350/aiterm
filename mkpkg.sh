#!/bin/bash

## part of aiterm project
## mkpkg.sh
## creates .tar.gz archive of source code
## By: Peter Talbott
## July-August 2026
## Updated 0.9.9-alpha

set -u

GET_NAME_FROM_PATH() {
    local prefix second last total count temp len name
    prefix=$(pwd)
    second=0
    last=0
    total=${#prefix}
    count=-1
    while [ $count -lt $total ]; do
        ((count++))
        temp=${prefix:$count:1}
        if [ "$temp" = "/" ] && [ $count -gt $last ]; then
            second=$last
            last=$count
        fi
    done
    len=$((last-second))
    name=${prefix:$((second+1)):$((len-1))}
    printf '%s\n' "$name"
}

GET_VERSION_FROM_PATH() {
    local prefix
    prefix=$(pwd)
    printf '%s\n' "${prefix##*/}"
}

PROJECT=$(GET_NAME_FROM_PATH)
VERSION=$(GET_VERSION_FROM_PATH)
ARCHIVE="$PROJECT-$VERSION.tar.gz"

# Files required for a functional source package.
REQUIRED_FILES=(
    "Makefile"
    "main.c"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        printf 'ERROR: required package file is missing: %s\n' "$file" >&2
        exit 1
    fi
done

# Use null-safe shell globs instead of ls, while keeping the package readable.
shopt -s nullglob
FILES_LIST=(
    *.c
    *.h
    *.xml
    Makefile*
    README.md
    LICENSE
    *.example
    aiterm-icon.png
    *.py
    *.sh
)

function MKLIST()
{
    COUNT=0
    for X in ${FILES_LIST[@]}; do
       ls -1 $X 2>/dev/null
       ((COUNT++))
    done
    return $COUNT
}

FILES=$(MKLIST)
TOTAL=$?
shopt -u nullglob

if [ -f "$ARCHIVE" ]; then
    rm -f -- "$ARCHIVE"
fi
printf 'Total files to Archive:  %s\n' "$TOTAL"
printf 'Creating Archive: %s\n' "$ARCHIVE"
tar --gzip -cf "$ARCHIVE" -C "$(pwd)" $FILES

if [ $? -eq 0 ]; then
    printf 'Success\n'
else
    printf 'Failure\n'
    exit 1
fi

printf 'Thats all folks!\n\n'
