#!/bin/sh
#
# watch.sh - Tail /var/log/messages filtered to USB events.

DUR=${DUR:-60}

echo "Watching /var/log/messages for USB events for ${DUR} seconds."
echo "Plug/unplug USB devices to generate events."

timeout "${DUR}" tail -F /var/log/messages | grep -iE 'usb|ugen|uftdi|uplcom|uslcom|myfirst_usb|uhub'
