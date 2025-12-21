#!/bin/bash
# Auto-increment seed_fw_version in hoopi.h
# Version is uint8_t, so it rolls over from 255 to 0

HEADER_FILE="hoopi.h"

# Check if file exists
if [ ! -f "$HEADER_FILE" ]; then
    echo "Error: $HEADER_FILE not found"
    exit 1
fi

# Extract current version number
CURRENT_VERSION=$(grep -E "^uint8_t seed_fw_version = [0-9]+;" "$HEADER_FILE" | sed -E 's/.*= ([0-9]+);.*/\1/')

if [ -z "$CURRENT_VERSION" ]; then
    echo "Error: Could not find seed_fw_version in $HEADER_FILE"
    exit 1
fi

# Increment version with uint8 rollover (1-255, wraps to 1)
NEW_VERSION=$(( (CURRENT_VERSION % 255) + 1 ))

# Update the file
sed -i.bak -E "s/^(uint8_t seed_fw_version = )[0-9]+;/\1$NEW_VERSION;/" "$HEADER_FILE"

# Remove backup file
rm -f "${HEADER_FILE}.bak"

echo "Version incremented: $CURRENT_VERSION -> $NEW_VERSION"
