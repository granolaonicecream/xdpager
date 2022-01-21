# XDPager

The X(org)D(esktop)Pager is an attempt at writing a pager similar to the workspace switcher of XFCE, without all the extra Desktop Environment (DE) dependencies. This is useful for those that prefer a non-DE setup like xmonad, i3, etc. but would still like an Exposé-lite feature.  XDPager assumes an EWMH compliant window manager. 

![XDPager Overview](screenshot.png)

## Features
- Shows a dynamic view of workspaces and windows on each when loaded
- Switches workspaces via XK_Return or mouse click
- Search for and activate windows by their className
- Draws UTF8 strings via Xft (e.g. for icon fonts)

## Installation
- Clone the repository
- Run `make`
- Execute `xdpager` or hook it up to a keybinding

## Dependencies
- libX11 (likely installed)
- libXft and freetype2 (likely installed)
- xdotool (commands to the window manager)

# Usage
XDPager provides a live view of the windows on all desktops sans-window content.  Each desktop is drawn to a grid cell and labeled with its respective desktop name in the bottom left corner.  XDPager has two operating modes: desktop and search.

## Desktop Mode
In desktop mode, the cell background of the currently selected desktop is highlighted.  The user can change the selection by use of the arrow keys, vi-navigation, or using hovering with the mouse.  The return key or mouse click will navigate to the selected desktop.

F2 can be used to toggle window text to none, className, and `_NET_WM_NAME`

The escape key is used to close XDPager.

## Search Mode
To enter search mode, press the forward slash key while in desktop mode.  The `stringPrefix` is display in the upper left corner of XDPager to signify this mode is active.

In search mode, the user can enter a text query that is equivalent to ``window.className.startsWith(query)``.  Windows that match this predicate are outlined and the current window selection is additionally filled with color.  The left/right arrow keys allow the user to rotate the selected window to the previous/next of the outlined windows.  Pressing return will activate the window, which may include switching desktops.

The escape key is used to exit search mode.


# Configuration
Config file loading is TBD; options are currently hardcoded in `main.c`.  This means XDPager will need to be rebuilt in order to show changes.

### navType
Changes the behavior of moving the workspace selection.

| navType | Description |
| ------- | ----------- |
| `NAV_NORMAL_SELECTION` | Workspace won't change until XK_Return is pressed or mouse click. |
| `NAV_MOVE_WITH_SELECTION` | Workspace will change when the selection changes. Typically used if xdpager is set as a sticky window. Consult your WM for limitations on sticky windows. |
| `NAV_MOVE_WITH_SELECTION_EXPERIMENTAL` | Workspace will change with the selection _and_ xdpager will move to that workspace. Visually distracting depending on compositor effects, time to unmap/map/redraw, etc. Not recommended for now. |
 
>  TODO: add videos demonstrating these differences

### nWorkspaces
The number of workspaces to render.  XDPager will only display workspaces [0, nWorkspaces].  This will be irrelvant if dynamic workspaces are ever implemented.

### workspacesPerRow
The number of workspaces to show per row in the grid.  If workspacesPerRow == nWorkspaces, XDPager renders a single row.  If workspacesPerRow == 1, XDPager renders a single column.

## searchPrefix
The string prefix to indicates XDPager is in search mode.  This string supports UTF8.

## colors
Everything is hardcoded.  Knock yourself out.

# Project Details
The following sections contain details you probably don't care about
### Limitations
- Filtering limited to alphanumeric characters until I figure out how unicode keylogging works.
- Number of desktops is statically defined.  Max number of windows is statically defined.  Dynamic desktop support should be possible with an extension watching the `_NET_NUM_DESKTOPS` atom on the root window.
- Assumptions about monitors, X screen setup, and resolution are currently hardcoded to my setup (2x 2560x1440 monitors in a horizontal layout). Dynamic discovery and layout is on the backlog.
### The problem with `_NET_CLIENT_LIST_STACKING`
 While XDPager relies on an EWMH compliant window manager, certain window managers (e.g. xmonad) don't fully comply with features they claim to support.  Ideally, XDPager could simply watch `_NET_CLIENT_LIST_STACKING` to determine which windows matter and which are above others.  However, when a window manager doesn't maintain correct stacking order in this list, there is no way to tell which windows should be drawn first without asking for the children of the root window.  Since the list of children has to be traversed anyway, XDPager just sources data from that.

## Window Manager Requirements
Most window managers that comply with EWMH shouldn't have a problem.
- `WM_STATE` to determine which windows are visible
- `WM_CLASS` to determine the className for a window
- `_NET_WM_DESKTOP` to determine which desktop a window is on
- `_NET_DESKTOP_NAMES` to determine the names of the desktops

# FAQ
> Why doesn't XDPager have live window content previews?  Gnome/Cinnamon/whoever has a real fullscreen exposé feature!

XDPager is written with tiling window managers in mind (xmonad, i3, etc.) which treat non-visible windows differently than ``<your favorite DE>``. Typically, a window manager will unmap windows that aren't visible.  The window managers of popular DEs cheat this by leaving offscreen window mapped.  This project is not attempting to solve this limitation of the intended audience.

> Why is this code so messy?

This is my first C program; I don't know what I'm doing

## Disclaimer
- The UTF8 decoding was copied from another repository (TODO: find that link...)
