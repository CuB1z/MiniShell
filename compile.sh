#!/bin/bash

# Define the build and output directories
BUILD_DIR=./build
BINARY_DIR=./bin
OUTPUT_DIR=./

# Define the usage message
USE="Usage: $0 [OPTION]\nOptions:\n  -h, --help\tDisplay this help message.\n  -d, --debug\tEnable debug mode."

# Define the debug mode
DEBUG=0

# Arguments Control
if [ $# -eq 1 ]
then
    if [[ $1 = "-h" ]] || [[ $1 = "--help" ]]
    then
        echo -e $USE
        exit 0
    elif [[ $1 = "-d" ]] || [[ $1 = "--debug" ]]
    then
        DEBUG=1
        echo "Debug mode enabled."
    else
        echo "Error: Invalid argument.\n"
        echo -e $USE
        exit 1
    fi
elif [ $# -gt 1 ]
then
    echo "Error: Unexpected number of arguments."
    echo -e $USE
    exit 1
fi

# Create the build directory if it doesn't exist and delete its contents
mkdir -p "$BUILD_DIR"
rm -f "$BUILD_DIR"/*

# [Program Compilation] ========================================>>

if [[ $DEBUG -eq 1 ]]
then
    gcc -c -Wall -Werror main.c -o "$BUILD_DIR/main.o" -D DEBUG
else
    gcc -c -Wall -Werror main.c -o "$BUILD_DIR/main.o"
fi

if [ $? -ne 0 ]
then
    echo "Error: The program failed to compile."
    exit 2
fi

gcc "$BUILD_DIR/main.o" "$BINARY_DIR/libparser.a" -o "$OUTPUT_DIR/main" -static

if [ $? -ne 0 ]
then
    echo "Error: The program failed to compile."
    exit 2
fi

# [Success Message] ============================================>>
echo "The program was compiled successfully."