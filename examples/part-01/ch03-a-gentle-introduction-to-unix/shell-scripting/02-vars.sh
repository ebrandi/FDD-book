#!/bin/sh
# vars.sh   demonstrate variables and proper quoting

name="dev"
greeting="Welcome"
# Double quotes preserve spaces and expand variables.
echo "$greeting, $name"
# Single quotes prevent expansion. This prints the literal characters.
echo '$greeting, $name'

# Command substitution captures output of a command.
today="$(date +%Y-%m-%d)"
echo "Today is $today"
