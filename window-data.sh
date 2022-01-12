#!/bin/sh

# Provides the list of managed(?) windows and their respective desktops and geometry.
# xdotool provides better geometry than wmctrl, but does need to be normalized in multihead setups.

# _NET_CLIENT_LIST_STACKING has the windows ordered as a stack, rightmost as top
# This matters for floating windows. As long as we draw windows in stack insertion order we should be good.
xprop -root _NET_CLIENT_LIST_STACKING | cut -d '#' -f 2 | sed 's/,/\n/g' | while read windowId; 
do
	geometry=$(xdotool getwindowgeometry --shell $windowId | cut -d '=' -f 2 | sed '1d;$d' | xargs)
	desktop=$(xdotool get_desktop_for_window $windowId)
	className=$(xprop WM_CLASS -id $windowId | cut -d '=' -f 2 | awk '{ gsub("\"","",$NF); print $NF }')
	if [ ! $className ==  "mypager" ]; then 
		echo $desktop $geometry $className $windowId
	fi
done

