#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // for tolower()
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include "utf8.h"

typedef struct {
	int workspace;
	int x;
	int y;
	int w;
	int h;
	char* className;
	unsigned long windowId;
} MiniWindow;

typedef struct {
	unsigned long* pixels;
	XftFont** fonts; // Fonts in fallback order
	int nFonts;
	XftColor* fontColor;
	GC selected;
	GC normal;
	GC workspace;
	GC matched;
} GfxContext;

typedef struct {
	char* buffer;
	unsigned short size;
	MiniWindow* selectedWindow; // pointer to the currently selected MiniWindow
	MiniWindow** matchedWindows; // array of MiniWindows that match search filter
	unsigned short nMatched;
	char* prefix; // a prefix string to indicate search mode is active
} SearchContext;

typedef struct {
	MiniWindow* previews;
	int nPreviews;
	Window* workspaces; // array of desktops
	char** workspaceNames; // names of workspaces (assumes same size and order as workspaces)
	XftDraw** draws;  // XFT draw surface for strings. size == nWorkspaces
	unsigned short nWorkspaces; // size of workspaces
	int selected; // index of currently selected workspace
	SearchContext* search; // the current search string
	char mode; // current mode of the pager.  0 - workspace, 1 - className search, 2 - ???
	
	int workspacesPerRow;
	Window mainWindow; // for drawing the search string
} Model;

// Has the pointer moved into a different child window than the current selection
// Probably a more efficient way to do this using x,y and math
int findPointerWorkspace(Window relative, Window workspaces[], unsigned int nWorkspaces) {

	int i;
	for (i=0; i<nWorkspaces; ++i)
		if (workspaces[i] == relative)
			return i;
	return -1;
}

void updateSearchContext(SearchContext* search, MiniWindow* previews, int nPreviews) {
	MiniWindow* prevSelection = search->selectedWindow;

	search->nMatched = 0;
	char found = 0;
	for(int i=0; i<nPreviews; ++i) {
		if (strncmp(previews[i].className,search->buffer,search->size) == 0) {
			search->matchedWindows[search->nMatched++] = &previews[i];
			if (prevSelection == &previews[i]) {
				found = True; // previous selection still matches, keep it selected
			}
		}
	}
	// Use the first match in the list (should be deterministic) if we don't already have a selection
	if (!found && search->nMatched > 0) {
		search->selectedWindow = search->matchedWindows[0];
	}
}

void drawUtfText(Display* dpy, XftDraw* draw, XftFont** fonts, int nFonts, XftColor* color, int x, int y,
		char* text, int len, int w) {

	int err, tw = 0;
	char *t, *next;
	unsigned int rune;
	XftFont* f;
	XGlyphInfo ext;

	for (t=text; t - text < len; t = next) {
		next = utf8_decode(t, &rune, &err);
		int i;
		for (i=0; i<nFonts; ++i) {
			if (XftCharExists(dpy, fonts[i] ,rune)) {
				f = fonts[i];
				break;
			}
		}
		if (i != nFonts) {
			XftTextExtentsUtf8(dpy,f,(XftChar8*)t, next - t, &ext);
			tw += ext.xOff;
			XftDrawStringUtf8(draw, color, f, x, y, (XftChar8*)t, next-t);
			x += ext.xOff;
		} else {
			fprintf(stderr,"char %s not in fonts\n", next);
		}

	}
}

void redraw(Display *dpy, int screen, int margin, GfxContext* colorsCtx, Model* m) {
	unsigned short nWorkspaces = m->nWorkspaces;
	MiniWindow* previews = m->previews;
	int nPreviews = m->nPreviews;
	SearchContext* search = m->search;
	Window* workspaces = m->workspaces;
	int selected = m->selected;

	int i=0;
	// Quick clear of all child windows to cleanup selected border
	// Background is reset in case selected has changed
	for(i=0;i<nWorkspaces;++i) {
		long pixel = colorsCtx->pixels[2]; // default bg
		if (m->mode == 0 && i == selected) {
			pixel = colorsCtx->pixels[0]; // selected workspace in workspace mode
		}
		XSetWindowBackground(dpy, workspaces[i], pixel);
		XClearWindow(dpy,workspaces[i]);
	}

	// Draw windows.  Note that order should be stacking order to ensure floating windows are drawn correctly
	for(i=0; i<nPreviews; ++i) {
		MiniWindow mw = previews[i];
		if (mw.workspace < nWorkspaces) {
			GC fillGC = colorsCtx->normal;
			GC outlineGC = colorsCtx->workspace;
			if (m->mode == 1 && search->size > 0) {
				if (&previews[i] == search->selectedWindow) {
					fillGC = colorsCtx->selected;
					outlineGC = colorsCtx->matched;
				} else if (strncmp(mw.className,search->buffer,search->size) == 0) {
					outlineGC = colorsCtx->selected;	
				}

			} else if (m->mode == 0 && mw.workspace == selected) {
				// Outline windows on the selected workspace in workspace mode
				// This lets us see floating windows
				outlineGC = colorsCtx->selected;
			}

			//printf("drawing rect %d (%d %d %d %d)\n",mw->workspace,mw->x,mw->y,mw->w,mw->h);
			XFillRectangle(dpy, workspaces[mw.workspace], fillGC, mw.x,mw.y,mw.w,mw.h);
			XDrawRectangle(dpy, workspaces[mw.workspace], outlineGC, mw.x,mw.y,mw.w,mw.h);
		}
	}

	// Draw workspace labels
	for(i=0; i<nWorkspaces; ++i) {
		drawUtfText(dpy, m->draws[i], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 5, 80,
				m->workspaceNames[i], strlen(m->workspaceNames[i]), 0);
	}

	// TODO: customize where search string is drawn
	// Draw search string after everything to ensure it's on top
	if (m->mode == 1) {
		int prefixLen = strlen(search->prefix);
		char sstring[search->size+prefixLen];
		strcpy(sstring, search->prefix);
		strcat(sstring,search->buffer);
		drawUtfText(dpy, m->draws[0], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 10,20,
			sstring, search->size + prefixLen, 0);
	}
}

Window createMainWindow(Display *dpy, int screen, unsigned short nWorkspaces, unsigned short workspacesPerRow) {
	/// TODO: Dynamically determine dimensions by scale factor, number of workspaces, and their layout
	// This code was written with 16:9 2560x1440 monitors
	int width =  ((160+2+5) * workspacesPerRow) + 5;
	int nRows = nWorkspaces/workspacesPerRow;
	if (nWorkspaces % workspacesPerRow != 0)
		nRows += 1;
	int height = (90+5) * nRows  + 10;
	
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 20, 20, width, height, 
			0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	// Set metadata on the window before mapping
	XClassHint *classHint = XAllocClassHint();
	classHint->res_name = "xdpager";
	classHint->res_class = "xdpager";
	XSetClassHint(dpy, win, classHint);
	XFree(classHint);

	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(dpy, win);

	return win;
}

// Get the data for the actual Window Previews
// TODO: It would be really inefficient to run this after every window move/resize,
// so would need to refactor this to run against an individual window ID
MiniWindow* getWindowData(unsigned short maxWindows, int *nWindows) {
	int PARSE_MAX = 100;
	FILE *fp;
	int status;
	char line[LINE_MAX];
	char delim[] = " \n";

	MiniWindow *miniWindows = (MiniWindow*)malloc(maxWindows * sizeof(MiniWindow));
	*nWindows = 0;

	// TODO: this could all be done in C
	// At least setup the script path to be relative to the installation directory
	// Returns lines in the format:
	// <desktop idx> <x> <y> <width> <height> <className> <windowID>
	// the geometry is absolute positioning, so need to normalize for monitor resolutions
	// TODO: apparently popen isn't standard. replace later
	fp = (FILE*) popen("/home/archie/xdpager/window-data.sh", "r");
	if (fp == NULL)
		exit(1);
	while (fgets(line, PARSE_MAX, fp) != NULL)
	{
		printf("%s", line);
		char *curr;
		int actuals[5]; // Parse values as they come in. Easier than storing then parsing
		char *className = malloc(20 * sizeof(char));
		unsigned long windowId;
		int i = 0;
		curr = strtok(line, delim);
		while (curr != NULL)
		{
			if (i < 5) {
				actuals[i] = atoi(curr);
			} else if (i == 5) {
				strcpy(className, curr);
				// lowercase normalize (can these contain unicode?)
				for(char* p=className; *p; p++) *p = tolower(*p);
			} else {
				windowId = strtoul(curr, (char**)0, 16);
			}
			i++;
			curr = strtok(NULL, delim);
		}

		// Normalize X position for multi-monitor setup
		if (actuals[1] >= 2560) {
			actuals[1] -= 2560;
		}
		// Linear transformation by scale factor
		// TODO: configurable scale factor (keep in mind proportion to window size)
		MiniWindow ptr;
		ptr.workspace = actuals[0];
		ptr.x = actuals[1] /16;
		ptr.y = actuals[2] /16;
		ptr.w = actuals[3] /16;
		ptr.h = actuals[4] /16;
		ptr.className = className;
		ptr.windowId = windowId;
		miniWindows[*nWindows] = ptr;

		*nWindows += 1;
	}

	status = pclose(fp);
	if (status == -1)
		exit(1);

	return miniWindows;
}

// Window Managers like XMonad implement _NET_CLIENT_LIST_STACKING but don't meet the spec
// Need to resort to be able to draw floting windows reliably
// Compare against the actual stacking order provided by the root window
void reorderWindows(Display* dpy, MiniWindow* previews, int nPreviews) {
	Window root;
	Window parent;
	Window *children;
	unsigned int nchildren;
	XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &nchildren);
	int sortIdx = 0;
	for (int a=0; a < nchildren; a++) {
		for (int x=0;x<nPreviews;x++) {
			if (previews[x].windowId == children[a]) {
				// printf("Found match, swapping %d and %d\n", x, sortIdx);
				MiniWindow tmp = previews[x];
				previews[x] = previews[sortIdx];
				previews[sortIdx] = tmp;
				sortIdx++;
				break;
			}
		}

	}
}

char** getWorkspaceNames(Display* dpy, int screen, int nWorkspaces) {
	Atom prop = XInternAtom(dpy,"_NET_DESKTOP_NAMES",False);
	Atom utf8String = XInternAtom(dpy,"UTF8_STRING",False);
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy,RootWindow(dpy,screen),prop,
			0,100,False,utf8String,
			&actualType,&format,&nitems,&bytesAfter, &value);
//	printf("s = %d Value = %s bytesAfter = %ld actualType = %ld format = %d nitems = %d\n", s, value, bytesAfter, actualType, format, nitems);
	
	char** names = malloc(nWorkspaces*sizeof(char**));
	int start = 0;
	int total = 0;
	for(unsigned int i=0;i<nitems;i++) {
		// printf("value[%d]=%c\n", i, value[i]);
		if (value[i] == '\0') {
			// hit the end of an array value
			int size = i - start;
			char* word = malloc(size * sizeof(char));
			strcpy(word,(char*)(value+start));
			names[total++] = word;
			// UTF8 debugging (raw bytes)
		//	printf("word %s\n", word);
		//	for(int b=0;b<strlen(word);b++) {
		//		printf("\tchar %d = %02hhx\n",b, (unsigned char)word[b]);
		//	}
			start = i+1;
			// TODO: assumes all workspaces have labels; not true by spec
			if (total == nWorkspaces)
				break;
		}
	}
	XFree(value);
	return names;
}

XftFont* initFont(Display* dpy, int screen, char* fontName) {
	// Load Font to use
	XftFont* font = XftFontOpenName(dpy, screen, fontName);
	if (!font) {
		printf("failed to open font %s\n", fontName);
		exit(1);
	}
	return font;
}

// Initializes everything we need for drawing to a Window/XftDraw
// TODO: might be able to ditch the xlib GCs in favor of using Xft lib's rectangle drawing
GfxContext* initColors(Display* dpy, int screen, char* normalFg, char* normalBg, char* selectedFg) {
	GfxContext* ctx = malloc(sizeof(GfxContext));
	ctx->pixels = malloc(3* sizeof(unsigned long));

	// Load Fonts
	// TODO: robustness, configurability, etc.
	int nFonts = 3;
	XftFont** fonts = malloc(nFonts * sizeof(XftFont*));
	fonts[0] = initFont(dpy, screen, "Font Awesome 5 Free Solid");
	fonts[1] = initFont(dpy, screen, "Font Awesome 5 Brands");
	fonts[2] = initFont(dpy, screen, "Liberation Sans");

	Visual* visual = DefaultVisual(dpy, screen);
	Colormap colormap = DefaultColormap(dpy,screen);
	XftColor* fontColor = malloc(sizeof(XftColor));
	if (!XftColorAllocName(dpy,visual,colormap,"#f2e750",fontColor)) {
		printf("Failed to allocate xft color %s\n", selectedFg);
		exit(1);
	}

	ctx->fonts = fonts;
	ctx->nFonts = nFonts;
	ctx->fontColor = fontColor;

	// Set the background color of the selected window to something different
	XColor parsedColor;
	XParseColor(dpy, colormap, selectedFg, &parsedColor);
	// TODO: Need to XFreeColors and XColor during cleanup
	XAllocColor(dpy, colormap, &parsedColor);
	ctx->pixels[0] = parsedColor.pixel;

	// Selected GC, when scaling is small, the normal outline hides the selected background color
	XGCValues gcv_sel;
	GC gc_sel;
	gcv_sel.foreground = parsedColor.pixel; // Used by drawRectangle to highlight window border if selected
	gcv_sel.background = WhitePixel(dpy,screen);
	gc_sel = XCreateGC(dpy, RootWindow(dpy,screen), GCForeground | GCBackground, &gcv_sel);
	ctx->selected = gc_sel;

	// Normal GC, used for drawing window previews
	XGCValues gcv;
	GC gc;
	XParseColor(dpy, colormap, normalFg, &parsedColor);
	XAllocColor(dpy, colormap, &parsedColor);
	gcv.foreground = parsedColor.pixel; // Color used by fillRectangle
	gcv.background = WhitePixel(dpy,screen); // Irrelevant rn
	gc = XCreateGC(dpy, RootWindow(dpy, screen), GCForeground | GCBackground, &gcv);
	ctx->normal = gc;
	ctx->pixels[1] = parsedColor.pixel;

	// Workspace GC, used to draw the worksapce and outline floating windows
	XGCValues gcv_ws;
	GC gc_ws;
	XParseColor(dpy, colormap, normalBg, &parsedColor);
	XAllocColor(dpy, colormap, &parsedColor);
	gcv_ws.foreground = parsedColor.pixel; // Used for workspace bg and drawRectangle to match window border color
	gcv_ws.background = WhitePixel(dpy,screen); // Irrelevant rn
	gc_ws = XCreateGC(dpy, RootWindow(dpy, screen), GCForeground | GCBackground, &gcv_ws);
	ctx->workspace = gc_ws;
	ctx->pixels[2] = parsedColor.pixel;

	// Matched GC, used to draw border for selected matched window
	XGCValues gcv_match;
	GC gc_match;
	gcv_match.foreground = WhitePixel(dpy,screen); // edit this for search highlighted border
	gcv_match.background = WhitePixel(dpy,screen); // ireelevant
	gc_match = XCreateGC(dpy, RootWindow(dpy,screen), GCForeground | GCBackground, &gcv_match);
	ctx->matched = gc_match;
	
	return ctx;
}

// Handle a keypress in the workspace mode
// returns whether or not we should exit afterwards
int workspaceKey(KeySym sym, Model* model, GfxContext* colorsCtx) {
	
	if (sym == XK_Escape) {
		return 1;
	} else if (sym == XK_Right || sym == XK_l) {
		model->selected = (model->selected + 1) % model->nWorkspaces;
	} else if (sym == XK_Left || sym == XK_h) {
		// Fuck it just branch
		model->selected = model->selected == 0 ? model->nWorkspaces - 1 : model->selected - 1;
	} else if (sym == XK_Down || sym == XK_j) {
		model->selected = (model->selected + model->workspacesPerRow) % model->nWorkspaces;
	} else if (sym== XK_Up || sym == XK_k) {
		int tmp_s = model->selected - model->workspacesPerRow;
		model->selected = tmp_s < 0 ? model->nWorkspaces + tmp_s : tmp_s;	
	} else if (sym == XK_Return) {
		char command[23*sizeof(char)];
		sprintf(command, "xdotool set_desktop %d", model->selected);
		system(command);
		return 1;
	} else if (sym == XK_slash) {
		model->mode = 1; // switch to search mode
	}

	return 0;
}

// Handle a keypress in the search  mode
// returns whether or not we should exit afterwards
int searchKey(KeySym sym, Model* model, GfxContext* colorsCtx) {
	
	SearchContext* search = model->search;
	if (sym == XK_Escape) {
		if (search->size > 0) {
			// If we have a search string, clear it instead of exiting
			search->buffer[0] = '\0';
			search->size = 0;
			updateSearchContext(search, model->previews, model->nPreviews);
		}
		model->mode = 0; // switch back to workspace mode
	} else if (sym == XK_Right) {
		if (search->selectedWindow && search->size > 0) {
			for(int k=0; k<search->nMatched; ++k) {
				if (search->matchedWindows[k] == search->selectedWindow) {
					search->selectedWindow = search->matchedWindows[(k+1)%search->nMatched];
					break;
				}
			}
		}
	} else if (sym == XK_Left) {
		if (search->selectedWindow && search->size > 0) {
			for(int k=0; k<search->nMatched; ++k) {
				if (search->matchedWindows[k] == search->selectedWindow) {
					int idx = k == 0 ? search->nMatched - 1 : k -1;
					search->selectedWindow = search->matchedWindows[idx];
					break;
				}
			}
		}
	} else if (sym == XK_Return) {
		if (search->selectedWindow && search->size > 0) {
			char command[35*sizeof(char)];
			sprintf(command, "xdotool windowactivate %ld", search->selectedWindow->windowId);
			system(command);
			return 1;
		}
	} else if ((sym >= XK_a && sym <= XK_z) || (sym >= XK_A && sym <= XK_Z)) {
		if (search->size < 20) {
			strncat(search->buffer, XKeysymToString(sym), 1);
			search->size++;
			updateSearchContext(search, model->previews, model->nPreviews);
		}
	} else if(sym == XK_BackSpace) {
		if (search->size > 0) {
			search->buffer[search->size-1] = '\0';
			search->size--;
			updateSearchContext(search, model->previews, model->nPreviews);
		}
	}

	return 0;
}


int main(int argc, char *argv[]) {
	Display *dpy;
	int screen;
	Visual* visual;
	Window win;
	XEvent event;

	// Config options
	unsigned short nWorkspaces = 9;      // Number of workspaces/desktops
	unsigned short workspacesPerRow = 3;
	unsigned short maxWindows = 30;      // Maximum number of windows to preview (TODO: dynamic arrays)
	int total = 0;                       // Total number of windows actually parsed
	int margin = 1;                      // An amount to pad windows with for visual clarity (Might not be necessary anymore with border drawing)
	char unreliableEwmhClientListStacking = 1;
	SearchContext* search = malloc(sizeof(SearchContext));
	search->buffer = malloc(20 * sizeof(char)); // buffer for searching by text
	search->selectedWindow = NULL;		// Give it a sensible default instead of random memory
	search->matchedWindows = malloc(maxWindows * sizeof(MiniWindow*)); // array for windows that match search string
								       // Overeager alloc, but lol dynamic arrays
	search->nMatched = 0;			// length of matchedWindows
	search->prefix = "";

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "Can't open display\n");
		exit(1);
	}

	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy,screen);
	win = createMainWindow(dpy,screen, nWorkspaces, workspacesPerRow);
	
	// TODO colors as configureable options.  Formatting
	GfxContext* colorsCtx = initColors(dpy, screen, "rgb:4d/52/57", "rgb:1d/1f/21", "rgb:f2/e7/50");

	// Create child windows for each workspace
	// Don't necessarily need child windows, but we don't have to keep track of separate offsets this way
	// (preserve 16:9 ratio)
	Window* workspaces = malloc(nWorkspaces * sizeof(Window));
	XftDraw** draws = malloc(nWorkspaces * sizeof(XftDraw*));
	int i;
	for (i=0;i<nWorkspaces;++i) {
		int width = 160 + margin;
		int height = 90 + margin;
		int x = 5 + ((i%workspacesPerRow) * (width+5));
		int y = 5 + ((i/workspacesPerRow) * (height+5)) ;
		workspaces[i] = XCreateSimpleWindow(dpy, win, x, y, width, height, 1, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
		XSelectInput(dpy, workspaces[i], ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
		XMapWindow(dpy, workspaces[i]);

		// Create Xft draw surface for text per window so we don't have to keep track of window position offsets
		draws[i] = XftDrawCreate(dpy,workspaces[i],visual,DefaultColormap(dpy,screen));
	}

	// Get readable workspace names 
	char** workspaceNames = getWorkspaceNames(dpy, screen, nWorkspaces);

	// Allocate an array of the actual windows to draw.
	// Geometry for each set of windows should be relative to its display's origin
	MiniWindow* miniWindows = getWindowData(maxWindows, &total);
	
	if (unreliableEwmhClientListStacking) 	
		reorderWindows(dpy, miniWindows, total);

	// Collect all this shit together for organization
	Model* model = malloc(sizeof(Model));
	model->workspaces = workspaces;
	model->workspaceNames = workspaceNames;
	model->draws = draws;
	model->nWorkspaces = nWorkspaces;
	model->previews = miniWindows;
	model->nPreviews = total;
	model->selected = 0;
	model->search = search;
	model->mainWindow = workspaces[0];
	model->mode = 0;
	model->workspacesPerRow = workspacesPerRow;

	while(1) {
		XNextEvent(dpy, &event);

		// Expose events
		// Redraw fully only on the last damaged event
		if (event.type == Expose && event.xexpose.count == 0) {
			//printf("Expose event for %d\n", event.xexpose.window);
			redraw(dpy,screen,margin,colorsCtx,model);
		}

		// Key events
		if (event.type == KeyPress) {
			KeySym sym = XLookupKeysym(&event.xkey, 0);
			//printf("keycode %d %s\n", event.xkey.keycode, XKeysymToString(sym));
			int shouldExit = 0;
			switch(model->mode) {
				case 0:
					shouldExit = workspaceKey(sym, model, colorsCtx);
					break;
				case 1:
					shouldExit = searchKey(sym, model, colorsCtx);
					break;
				default: 
					printf("Unknown mode %d\n",model->mode);
					shouldExit = 1;
					break;
			}
			if (shouldExit)
				break; // goto cleanup
			else
				redraw(dpy,screen,margin,colorsCtx,model);
		}
		
		// Mouse movement
		// Mouse selection of filtered windows not implemented. Would need to do geometry range checking
		if (event.type == MotionNotify && model->mode == 0) {
			int pWorkspace = findPointerWorkspace(event.xmotion.window, workspaces, nWorkspaces);
			if (pWorkspace != model->selected){
				model->selected = pWorkspace;
				redraw(dpy,screen,margin,colorsCtx,model);
			}
		}

		// If a childwindow is clicked, move to the workspace
		if (event.type == ButtonRelease && model->mode == 0) {
			for (i=0;i<nWorkspaces;++i) {
				if (event.xany.window == workspaces[i]) {
					break;
				}
			}
			if (i < nWorkspaces) {
				// Assumes desktop numbers are at most double digit
				char command[23*sizeof(char)];
				sprintf(command, "xdotool set_desktop %d", i);
				system(command);
			}
			// Always termiante even if we don't move
			break;
		}
	}


	// Cleanup
	XftColorFree(dpy,visual,DefaultColormap(dpy,screen),colorsCtx->fontColor);
	XFreeColors(dpy,DefaultColormap(dpy,screen),colorsCtx->pixels,3,0l);
	XFree(colorsCtx->normal);
	XFree(colorsCtx->selected);
	XFree(colorsCtx->workspace);
	XFree(colorsCtx->matched);
	free(colorsCtx->pixels);
	free(colorsCtx);

	free(model->previews);
	free(model->workspaces);
	for(i=0;i<nWorkspaces;++i) {
		free(model->workspaceNames[i]);
	}
	free(model->workspaceNames);
	free(model->search->buffer);
	free(model->search);
	free(model);

	return 0;
}
