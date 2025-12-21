#!/bin/bash
# Convenience script to increment version and build firmware

set -e

echo "Incrementing firmware version..."
./increment_version.sh

echo "Building firmware..."
make -j4 "$@"
