#!/bin/sh
# hello.sh   a first shell script using FreeBSD's native /bin/sh
# Print a friendly message with the current date and the active user.

# 'date' prints the current date and time
# 'whoami' prints the current user
echo "Hello from FreeBSD!"
echo "Date: $(date)"
echo "User: $(whoami)"
