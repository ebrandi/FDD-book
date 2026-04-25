#!/bin/sh
#
# devd_watch.sh: subscribe to the devd event stream and print events
# that match the myfirst driver. Used for Chapter 8 Challenge 5 and
# for debugging driver load/unload behavior.
#
# Run in one terminal while you load and unload the driver in another.
# Requires netcat (base system) to connect to devd's socket.

SOCKET=/var/run/devd.seqpacket.pipe

if [ ! -S "${SOCKET}" ]; then
    echo "devd socket ${SOCKET} not available" >&2
    echo "ensure devd is running (service devd status)" >&2
    exit 1
fi

echo "Listening for myfirst events on ${SOCKET}..." >&2
nc -U "${SOCKET}" | grep -i 'myfirst'
