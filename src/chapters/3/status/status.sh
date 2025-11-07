#!/bin/sh
# status.sh   show exit codes and conditional chaining

# Try to list a directory that exists. 'ls' should return 0.
ls /etc && echo "Listing /etc succeeded"

# Try something that fails. 'false' always returns nonzero.
false || echo "Previous command failed, so this message appears"

# You can test the last status explicitly using $?
echo "Last status was $?"
