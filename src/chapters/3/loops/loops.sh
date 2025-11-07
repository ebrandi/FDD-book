#!/bin/sh
# loops.sh   for and while loops in /bin/sh

# A 'for' loop over pathnames. Always quote expansions to handle spaces safely.
for f in /etc/*.conf; do
  echo "Found conf file: $f"
done

# A 'while' loop to read lines from a file safely.
# The 'IFS=' and 'read -r' avoid trimming spaces and backslash escapes.
count=0
while IFS= read -r line; do
  count=$((count + 1))
done < /etc/hosts
echo "The /etc/hosts file has $count lines"
