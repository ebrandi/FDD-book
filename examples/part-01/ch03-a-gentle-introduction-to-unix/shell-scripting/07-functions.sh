#!/bin/sh
# functions.sh - Demonstrates using functions and command-line arguments in a shell script.
#
# Usage:
#   ./functions.sh NUM1 NUM2
# Example:
#   ./functions.sh 5 7
#   This will output: "[INFO] 5 + 7 = 12"

# A simple function to print informational messages
say() {
  # "$1" represents the first argument passed to the function
  echo "[INFO] $1"
}

# A function to sum two integers
sum() {
  # "$1" and "$2" are the first and second arguments
  local a="$1"
  local b="$2"

  # Perform arithmetic expansion to add them
  echo $((a + b))
}

# --- Main script execution starts here ---

# Make sure the user provided two arguments
if [ $# -ne 2 ]; then
  echo "Usage: $0 NUM1 NUM2"
  exit 1
fi

say "Beginning work"

# Call the sum() function with the provided arguments
result="$(sum "$1" "$2")"

# Print the result in a nice format
say "$1 + $2 = $result"
