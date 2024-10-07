#!/bin/bash

# Directory to save the files
DIR="../pa2"
mkdir -p "$DIR"

# Generate 1 million files
for i in $(seq 1 100000); do
            # Generate a unique file name using the loop counter
                FILENAME="$DIR/file_${i}.txt"
                    # Use dd to generate a file of size 1KB with random data
                dd if=/dev/urandom of="$FILENAME" bs=1024 count=1 >/dev/null 2>&1
                done
                echo "100000 10KB files generated in $DIR."
