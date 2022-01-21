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
#include "llist.c"

#define NAV_NORMAL_SELECTION 0
#define NAV_MOVE_WITH_SELECTION 1
#define NAV_MOVE_WITH_SELECTION_EXPERIMENTAL 2

char navType = NAV_NORMAL_SELECTION;

typedef struct {
	unsigned short nWorkspaces;      // Number of workspaces/desktops
	unsigned short workspacesPerRow; // Number of workspaces to show per row
	unsigned short maxWindows;      // Maximum number of windows to preview (TODO: dynamic arrays)
	char* searchPrefix; // Prefix to show while in search mode
} XDConfig;

typedef struct {
	int workspace;
	int x;
	int y;
	int w;
	int h;
	char* className;
	char* name;
	unsigned long windowId;
} MiniWindow;

typedef struct {
	unsigned long* pixels;
	XftFont** fonts; // Fonts in fallback order
	XftFont** wFonts; // Fonts for optional window text
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
	llist* previews;
	Window* workspaces; // array of desktops
	char** workspaceNames; // names of workspaces (assumes same size and order as workspaces)
	XftDraw** draws;  // XFT draw surface for strings. size == nWorkspaces
	unsigned short nWorkspaces; // size of workspaces
	int selected; // index of currently selected workspace
	SearchContext* search; // the current search string
	char mode; // current mode of the pager.  0 - workspace, 1 - className search, 2 - ???
	char windowTextMode; // 0 - no text, 1 - className, 2 - name/title
	
	int workspacesPerRow;
	Window mainWindow; // for drawing the search string
	int previewWidth;  // Width of one mini window
	int previewHeight; // Height of one mini window
} Model;

// TODO: how does control flow work here? Program keeps running 
// but are things still being allocated?
// Error handling for complex windows
int errorHandler(Display* dpy, XErrorEvent* event) {
	// Windows like firefox create and close windows 
	// really quick on initial startup. These aren't 
	// fatal errors for us, so we ignore them.
	if (event->error_code == BadWindow) {
		printf("BadWindow! \n");
	} else {
		// Still cleanup and exit if we hit something else
		printf("Other error %d\n", event->error_code);
		exit(1);
	}
	return 0;
}

// TODO: Move to a navigation file
void setDesktopForWindow(Window win, int desktop) {
	char command[50*sizeof(char)];
	sprintf(command, "xdotool set_desktop_for_window %ld %d", win, desktop);
	system(command);
}

void activateWindow(Window win) {
	char command[35*sizeof(char)];
	sprintf(command, "xdotool windowactivate %ld", win);
	system(command);
}

void switchDesktop(int desktop) {
	char command[35*sizeof(char)];
	sprintf(command, "xdotool set_desktop %d", desktop);
	system(command);
}

void grabFocus(Window win) {
	char command[35*sizeof(char)];
	sprintf(command, "xdotool windowfocus %ld", win);
	system(command);
}

// Has the pointer moved into a different child window than the current selection
// Probably a more efficient way to do this using x,y and math
int findPointerWorkspace(Window relative, Window workspaces[], unsigned int nWorkspaces) {

	int i;
	for (i=0; i<nWorkspaces; ++i)
		if (workspaces[i] == relative)
			return i;
	return -1;
}

void updateSearchContext(SearchContext* search, llist* previews) {
	MiniWindow* prevSelection = search->selectedWindow;

	search->nMatched = 0;
	char found = 0;
	node* ptr = previews->head;
	for(int i=0; i<previews->size; ++i) {
		MiniWindow* mw = ptr->data;
		if (strncmp(mw->className,search->buffer,search->size) == 0) {
			search->matchedWindows[search->nMatched++] = mw;
			if (prevSelection == mw) {
				found = True; // previous selection still matches, keep it selected
			}
		}
		ptr = ptr->next;
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
			if (w >= 0 && tw >= w) {
				break;
			}
			XftDrawStringUtf8(draw, color, f, x, y, (XftChar8*)t, next-t);
			x += ext.xOff;
		} else {
			fprintf(stderr,"char %s not in fonts\n", next);
		}

	}
}

void redraw(Display *dpy, int screen, int margin, GfxContext* colorsCtx, Model* m) {
	unsigned short nWorkspaces = m->nWorkspaces;
	llist* previews = m->previews;
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
	node* ptr = previews->head;
	for(i=0; i< previews->size; ++i) {
		MiniWindow mw = *(MiniWindow *)ptr->data;
		if (mw.workspace < nWorkspaces) {
			GC fillGC = colorsCtx->normal;
			GC outlineGC = colorsCtx->workspace;
			if (m->mode == 1 && search->size > 0) {
				if (ptr->data == search->selectedWindow) {
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

			// draw title text
			switch (m->windowTextMode) {
				case 0: break; // No window text
				case 1: 
					if (mw.className)
					drawUtfText(dpy, m->draws[mw.workspace], colorsCtx->wFonts, 
						colorsCtx->nFonts, colorsCtx->fontColor, 
						mw.x, mw.y+10, mw.className, strlen(mw.className), mw.w);
					break;
				case 2:
					if (mw.name)
					drawUtfText(dpy, m->draws[mw.workspace], colorsCtx->wFonts, 
						colorsCtx->nFonts, colorsCtx->fontColor, 
						mw.x, mw.y+10, mw.name, strlen(mw.name), mw.w);
					break;
			}
		}
		ptr = ptr->next;
	}

	// Draw workspace labels
	for(i=0; i<nWorkspaces; ++i) {
		drawUtfText(dpy, m->draws[i], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 5, m->previewHeight-10,
				m->workspaceNames[i], strlen(m->workspaceNames[i]), -1);
	}

	// TODO: customize where search string is drawn
	// Draw search string after everything to ensure it's on top
	if (m->mode == 1) {
		int prefixLen = strlen(search->prefix);
		char sstring[search->size+prefixLen];
		strcpy(sstring, search->prefix);
		strcat(sstring,search->buffer);
		drawUtfText(dpy, m->draws[0], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 10,20,
			sstring, search->size + prefixLen, -1);
	}
}


void setTitle(Display* dpy, Window w) {
	char* title = "XDPager";
	XChangeProperty(dpy, w, 
			XInternAtom(dpy, "_NET_WM_NAME", False),
			XInternAtom(dpy, "UTF8_STRING", False),
			8, PropModeReplace, (unsigned char*)title, strlen(title));
}

int MARGIN = 2;
Window createMainWindow(Display *dpy, int screen, unsigned short nWorkspaces, unsigned short workspacesPerRow,
		int* return_width, int* return_height) {
	/// TODO: Dynamically determine dimensions by scale factor, number of workspaces, and their layout
	// This code was written with 16:9 2560x1440 monitors
	*return_width =  ((160 + MARGIN * 2) * workspacesPerRow);
	int nRows = nWorkspaces/workspacesPerRow;
	if (nWorkspaces % workspacesPerRow != 0)
		nRows += 1;
	*return_height = (90 + MARGIN * 2) * nRows;
	
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 20, 100, *return_width, *return_height, 
			0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	// Set metadata on the window before mapping
	XClassHint *classHint = XAllocClassHint();
	classHint->res_name = "xdpager";
	classHint->res_class = "xdpager";
	XSetClassHint(dpy, win, classHint);
	XFree(classHint);

	setTitle(dpy, win);

	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(dpy, win);

	return win;
}

char* getStringProp(Display* dpy, Window w, Atom prop, Atom type) {
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, w, prop,
			0,100,False,type,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		char* result = malloc(nitems*sizeof(char) + 1);
		if (result == NULL)
			puts("uh oh getStringProp");
		strcpy(result,(char*)value);
		XFree(value);
		return result;
	}
	return NULL;
}

int getWmDesktop(Display* dpy, Window w) {
	Atom prop = XInternAtom(dpy,"_NET_WM_DESKTOP",False);
	Atom cardinal = XInternAtom(dpy,"CARDINAL",False);
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, w, prop,
			0,100,False,cardinal,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		int result = *(int*)value;
		XFree(value);
		return result;
	}
	return -1;
}

Atom* getAtomProp(Display* dpy, Window w, Atom prop, int* return_nitems) {
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, w, prop,
			0,100,False,AnyPropertyType,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		Atom* result = malloc(nitems*sizeof(Atom));
		if (result == NULL)
			puts("Uh oh getAtomProp");
		memcpy(result,(Atom*)value, nitems*sizeof(Atom));
		*return_nitems = nitems;
		puts("free atom");
		XFree(value);
		return result;
	}
	return NULL;
}

char* getWmName(Display* dpy, Window w) {
	Atom prop = XInternAtom(dpy,"_NET_WM_NAME",False);
	Atom utf8String = XInternAtom(dpy,"UTF8_STRING",False);
	return getStringProp(dpy, w, prop, utf8String);
}

char* getClassName(Display* dpy, Window w) {
	Atom prop = XInternAtom(dpy,"WM_CLASS",False);
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, w, prop,
			0,100,False,AnyPropertyType,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		char* result = NULL;
		char* instance = (char*)value;
		char* className = instance + strlen(instance) + 1;
		if (className - instance < nitems)  {
			// save one iteration by hardcoding a max size
			result = malloc(30 * sizeof(char));
			if (result == NULL)
				puts("Uh oh getClassName");
			strcpy(result,className);
		}
		XFree(value);
		return result;
	}
	return NULL;
}

long getWmState(Display* dpy, Window w, int* return_nitems) {
	Atom prop = XInternAtom(dpy,"WM_STATE",False);
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, w, prop,
			0,100,False,AnyPropertyType,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		long result = *(long*)value;
		*return_nitems = nitems;
		XFree(value);
		return result;
	}
	return 0;
}

MiniWindow* makeMiniWindow(int workspace, int x, int y, int width, int height, 
		char* className, char* name, Window window, double s_x, double s_y) {

	// normalize coordinates TODO: dynamic
	if (x >= 2560) 
		x -= 2560;

	// destructive but who cares
	for(char* p=className; *p; p++) *p = tolower(*p);

	// Scale factor is divisor
	MiniWindow* mw = malloc(sizeof(MiniWindow));
	if (mw == NULL)
		puts("Uh oh makeMiniWindow");
	mw->workspace = workspace;
	mw->x = x / s_x;
	mw->y = y / s_y;
	mw->w = width / s_x;
	mw->h = height / s_y;
	mw->className = className;
	mw->name = name;
	mw->windowId = window;

	return mw;
}

llist* testX(Display* dpy, double s_x, double s_y) {
	Window root;
	Window parent;
	Window *children;
	unsigned int nchildren;
	XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &nchildren);

	llist* miniWindows = llist_create();
	
	for (int a=0; a < nchildren; a++) {
		XWindowAttributes wattr;
		XGetWindowAttributes(dpy,children[a],&wattr);

	//	printf("Start 0x%lx\n", children[a]);
		char* name = getWmName(dpy,children[a]);
		char* className = getClassName(dpy, children[a]);
		int nitems = 0;
		long state = getWmState(dpy,children[a],&nitems);
		int desktop = getWmDesktop(dpy,children[a]);
		if (state != 0 && desktop != -1) {
	//		printf("%d %d %d %d %d %s %s 0x%lx\n",desktop, 
	//			wattr.x, wattr.y, wattr.width, wattr.height, 
	//			className, name, children[a]);
			MiniWindow* mw = makeMiniWindow(desktop,
				wattr.x, wattr.y, wattr.width, wattr.height, 
				className, name, children[a], s_x, s_y);
			llist_addBack(miniWindows,mw);
		}
	}

	return miniWindows;
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
	if(names == NULL)
		puts("Uh oh getWorkspaceNames");
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

	XftFont* windowFont = initFont(dpy, screen, "Liberation Sans:pixelsize=10");
	XftFont** wFonts = malloc(nFonts * sizeof(XftFont*));
	memcpy(wFonts,fonts,nFonts*sizeof(XftFont*));
	wFonts[2] = windowFont;

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
	ctx->wFonts = wFonts;

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
int workspaceKey(KeySym sym, Model* model, GfxContext* colorsCtx, Window wMain) {
	
	int oldSelected = model->selected;
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
	} else if (sym == XK_F2) {
		model->windowTextMode = (model->windowTextMode + 1) % 3;
	}

	// If using interactive selection navigation
	if (oldSelected != model->selected) {
		if (navType == NAV_MOVE_WITH_SELECTION) {
			switchDesktop(model->selected);
			grabFocus(wMain);
		} else if (navType == NAV_MOVE_WITH_SELECTION_EXPERIMENTAL) {
			setDesktopForWindow(wMain, model->selected);
			activateWindow(wMain);
		}
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
			updateSearchContext(search, model->previews);
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
			updateSearchContext(search, model->previews);
		}
	} else if(sym == XK_BackSpace) {
		if (search->size > 0) {
			search->buffer[search->size-1] = '\0';
			search->size--;
			updateSearchContext(search, model->previews);
		}
	}

	return 0;
}

void cleanupList(llist* list) {
	while (list->size > 0) {
		MiniWindow* mw = llist_remove(list, 0);
		if (mw->className)
			free(mw->className);
		if (mw->name)
			free(mw->name);
		free(mw);
	}
	free(list);
}

// If the main window has been resized, adjust the child windows aspect ratio,
// no matter how dumb it looks
void resizeWorkspaceWindows(Display* dpy, Window* workspaces, int nWorkspaces, int width, int height,
		int workspacesPerRow, int* return_width, int* return_height) {

	int nRows = nWorkspaces/workspacesPerRow;
	if (nWorkspaces % workspacesPerRow != 0)
		nRows += 1;
	int windowWidth = (width + MARGIN*2) / workspacesPerRow;
	int windowHeight = (height + MARGIN*2) / nRows;
	for (int i=0; i<nWorkspaces; i++) {
		int x = ((i%workspacesPerRow) * (windowWidth + MARGIN));
		int y = ((i/workspacesPerRow) * (windowHeight + MARGIN));
		//printf("resize %d %d %d %d\n", x, y, windowWidth, windowHeight);
		XMoveResizeWindow(dpy, workspaces[i], x, y, windowWidth, windowHeight);
	}

	*return_width = windowWidth;
	*return_height = windowHeight;

}

char isWorkspaceWindow(Window* workspaces, int nWorkspaces, Window w) {
	for (int i=0; i<nWorkspaces; i++) {
		if (workspaces[i] == w)
			return 1;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	XSetErrorHandler(errorHandler);
	Display *dpy;
	int screen;
	Visual* visual;
	Window win;
	XEvent event;

	// Config options
	unsigned short nWorkspaces = 9;      // Number of workspaces/desktops
	unsigned short workspacesPerRow = 3;
	unsigned short maxWindows = 30;      // Maximum number of windows to preview (TODO: dynamic arrays)
	//int total = 0;                       // Total number of windows actually parsed
	int margin = 1;                      // An amount to pad windows with for visual clarity (Might not be necessary anymore with border drawing)
	//char unreliableEwmhClientListStacking = 1;
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

	// Set Root window to notify us of its child windows' events
	XSetWindowAttributes attrs;
	attrs.event_mask = SubstructureNotifyMask;
	XChangeWindowAttributes(dpy,DefaultRootWindow(dpy),CWEventMask,&attrs);

	int width_old = 160;
	int height_old = 90;
	win = createMainWindow(dpy,screen, nWorkspaces, workspacesPerRow, &width_old, &height_old);
	
	// TODO colors as configureable options.  Formatting
	GfxContext* colorsCtx = initColors(dpy, screen, "rgb:4d/52/57", "rgb:1d/1f/21", "rgb:f2/e7/50");

	// Create child windows for each workspace
	// Don't necessarily need child windows, but we don't have to keep track of separate offsets this way
	// (preserve 16:9 ratio)
	Window* workspaces = malloc(nWorkspaces * sizeof(Window));
	XftDraw** draws = malloc(nWorkspaces * sizeof(XftDraw*));
	int i;
	int width = 160;
	int height = 90;
	for (i=0;i<nWorkspaces;++i) {
		int width = 160;
		int height = 90;
		int x =  ((i%workspacesPerRow) * (width + MARGIN)) + MARGIN;
		int y =  ((i/workspacesPerRow) * (height + MARGIN))+ MARGIN;
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
	double s_x = 16;
	double s_y = 16;
	llist* miniWindows = testX(dpy, s_x, s_y);

	// Collect all this shit together for organization
	Model* model = malloc(sizeof(Model));
	model->workspaces = workspaces;
	model->workspaceNames = workspaceNames;
	model->draws = draws;
	model->nWorkspaces = nWorkspaces;
	model->previews = miniWindows;
	model->selected = 0;
	model->search = search;
	model->mainWindow = workspaces[0];
	model->mode = 0;
	model->windowTextMode = 0;
	model->workspacesPerRow = workspacesPerRow;
	model->previewWidth = width;
	model->previewHeight = height;

	int nRows = nWorkspaces / workspacesPerRow;
	if (nWorkspaces % workspacesPerRow != 0)
		nRows++;

	while(1) {
		XNextEvent(dpy, &event);

		// Window resize events
		if (event.type == ConfigureNotify) {
//			printf("ConfigureNotify %lx %lx (%d,%d,%d,%d) %lx\n", 
//					event.xconfigure.window, event.xconfigure.event,
//					event.xconfigure.x, event.xconfigure.y, 
//					event.xconfigure.width, event.xconfigure.height,
//					event.xconfigure.above);
//			TODO: Ignore events from our child windows as they don't require redraws
			cleanupList(model->previews);
			if (event.xconfigure.window == win) {
				// If our window has been resized, update the scale factors
				int w = event.xconfigure.width;
				int h = event.xconfigure.height; 
				if (width_old != w || height_old != h) {
					s_x = 2560.0 / (((double)w)/workspacesPerRow);
					s_y = 1440.0 / (((double)h)/nRows);
					printf("resize main %d %d %d %d\n", w, h, width_old, height_old);
					width_old = w;
					height_old = h;
					resizeWorkspaceWindows(dpy, workspaces, nWorkspaces, w, h, workspacesPerRow, &width, &height);
					model->previewWidth = width;
					model->previewHeight = height;
				}
			} else if (isWorkspaceWindow(workspaces, nWorkspaces, event.xconfigure.window)) {
				printf("notify on child window 0x%lx\n", event.xconfigure.window);
			}
			model->previews = testX(dpy, s_x, s_y);
			redraw(dpy,screen,margin,colorsCtx,model);
		}

		// Expose events
		// Redraw fully only on the last damaged event
		if (event.type == Expose && event.xexpose.count == 0) {
			//printf("Expose event for %lx\n", event.xexpose.window);
			redraw(dpy,screen,margin,colorsCtx,model);
			if (event.xany.window == win) {
				grabFocus(win);
			}
		}

		// Key events
		if (event.type == KeyPress) {
			KeySym sym = XLookupKeysym(&event.xkey, 0);
			//printf("keycode %d %s\n", event.xkey.keycode, XKeysymToString(sym));
			int shouldExit = 0;
			switch(model->mode) {
				case 0:
					shouldExit = workspaceKey(sym, model, colorsCtx, win);
					break;
				case 1:
					shouldExit = searchKey(sym, model, colorsCtx);
					break;
				default: 
					printf("Unknown mode %d\n",model->mode);
					shouldExit = 1;
					break;
			}
			if (shouldExit == 1)
				break; // goto cleanup
			else {
				redraw(dpy,screen,margin,colorsCtx,model);
				grabFocus(win);
			}
		}
		
		// Mouse movement
		// Mouse selection of filtered windows not implemented. Would need to do geometry range checking
		if (event.type == MotionNotify && model->mode == 0) {
			int pWorkspace = findPointerWorkspace(event.xmotion.window, workspaces, nWorkspaces);
			if (pWorkspace != model->selected){
				model->selected = pWorkspace;
				redraw(dpy,screen,margin,colorsCtx,model);
				if (navType == NAV_MOVE_WITH_SELECTION) {
					switchDesktop(model->selected);
					grabFocus(win);
				} else if (navType == NAV_MOVE_WITH_SELECTION_EXPERIMENTAL) {
					setDesktopForWindow(win, model->selected);
					activateWindow(win);
				}
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

//	free(model->previews);
	free(model->workspaces);
	for(i=0;i<nWorkspaces;++i) {
		free(model->workspaceNames[i]);
	}
	free(model->workspaceNames);
	free(model->search->buffer);
	free(model->search);
	free(model);

	cleanupList(miniWindows);
	return 0;
}
