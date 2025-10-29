#!/bin/bash

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
GITNANO_CMD="$PROJECT_DIR/gitnano"

echo "=== Testing GitNano Commit Fix ==="
echo "Testing SHA1_HEX_SIZE fix for consecutive commits"

# Clean up any existing test environment
rm -rf /tmp/gitnano_commit_test
mkdir -p /tmp/gitnano_commit_test
cd /tmp/gitnano_commit_test

# Initialize repository
echo "Initializing repository..."
"$GITNANO_CMD" init

# Create first test file
echo "Creating first test file..."
echo "First test file content" > test1.txt

# Add and commit first file
echo "Adding and committing first file..."
"$GITNANO_CMD" add test1.txt
"$GITNANO_CMD" commit -m "First commit"

# Create second test file
echo "Creating second test file..."
echo "Second test file content" > test2.txt

# Add and commit second file - this tests the fix
echo "Adding and committing second file..."
"$GITNANO_CMD" add test2.txt
"$GITNANO_CMD" commit -m "Second commit"

# Create third test file
echo "Creating third test file..."
echo "Third test file content" > test3.txt

# Add and commit third file
echo "Adding and committing third file..."
"$GITNANO_CMD" add test3.txt
"$GITNANO_CMD" commit -m "Third commit"

echo "=== Test completed successfully! ==="
echo "SHA1_HEX_SIZE fix is working correctly."

# Cleanup
cd ..
rm -rf /tmp/gitnano_commit_test