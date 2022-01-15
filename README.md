This is very much a POC for an XFCE-style window pager.  It's also my first C program so I don't know what I'm doing.  All the configuration is hardcoded right now, but it should be mostly obvious

Features
- Shows a static view of the workspaces and windows on each when loaded
- Clicking on a workspace or pressing Return to switch workspaces
- Search for and activate windows by their className

Configuration
Config file loading is TBD; currently hardcoded configuration options
The value set for `navType` changes the behavior of changing the workspace selection.
| navType | Description |
| `NAV_NORMAL_SELECTION` | Workspace won't change until Return is pressed |
| `NAV_MOVE_WITH_SELECTION` | Workspace will change when the selection changes. Typically used if xdpager is set as a sticky window. Consult your WMfor limitations |
| `NAV_MOVE_WITH_SELECTION_EXPERIMENTAL` | Workspace will change with the selection _and_ xdpager will move to that workspace. Visually distracting depending on compositor effects, time to unmap/map/redraw, etc. Not recommended for now. |

Installation
- Update the hardcoded path to window-data.sh in main.c to the absolute path on your system.  I'll fix this eventually
- Run `make`
- Execute `xdpager` or hook it up to your window manager

Dependencies
- X11 (obviously)
- Xft and freetype2 (likely installed)
- xdotool (commands to the window manager, window geometry querying)
- xprop (get the list of virtual desktop names, get stacking list of window IDs)
- bash, sed, awk, cut (string manipulation of xdotool and xprop)

Limitations
- Loading data could technically be done in the C code, but I hate C strings and wanted to do as little string processing as possible.
- The window set is statically loaded once.  This means no window moving/new windows/window closing will be tracked. 
- Filtering limited to alphanumeric characters until I figure out how unicode keylogging works.
- Number of desktops is statically defined.  Max number of windows is statically defined.
  I don't have a dynamic array implementation and didn't want to get more frustrated with this language.
- Currently relies on `_NET_CLIENT_LIST_STACKING` to determine which windows are above others. Pretty sure this is wrong

Compromises
- Had to use Xft.h in order to support UTF8 strings for workspace names (glyph/icon fonts).
  This means depending on both the xft libs and the freetype2 fonts.  You __probably__ have these installed anyway.
- Some inefficient pieces of code where associate arrays or tries would come in handy.  I don't know how to do these in C.

Additional Notes
- The UTF8 decoding was copied from another repository (TODO: find that link...)
