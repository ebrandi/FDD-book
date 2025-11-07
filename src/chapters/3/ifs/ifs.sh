#!/bin/sh
# ifs.sh   demonstrate file and numeric tests

file="/etc/rc.conf"

# -f tests if a regular file exists
if [ -f "$file" ]; then
  echo "$file exists"
else
  echo "$file does not exist"
fi

num=5
if [ "$num" -gt 3 ]; then
  echo "$num is greater than 3"
fi

# String tests
user="$(whoami)"
if [ "$user" = "root" ]; then
  echo "You are root"
else
  echo "You are $user"
fi
