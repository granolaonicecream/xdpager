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
#include "config.c"

#define NAV_NORMAL_SELECTION 1
#define NAV_MOVE_WITH_SELECTION 2
#define NAV_MOVE_WITH_SELECTION_EXPERIMENTAL 3

char navType = NAV_NORMAL_SELECTION;

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
	int previewWidth;  // Dimensions of one mini window
	int previewHeight; 
	int width;	   // Dimensions of the main window
	int height;	   
	double s_x;	   // Scaling factors of previews
	double s_y;	   
} Sizing;

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
	Sizing* sizing;    // Information about the current size and scaling
	GfxContext* gfx;
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
	if (search->nMatched == 0) {
		search->selectedWindow = NULL;
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

	// Window text offset based on pixelsize of fonts
	int minDimension = m->sizing->previewHeight < m->sizing->previewWidth ? 
		m->sizing->previewHeight : m->sizing->previewWidth;
	int pixelsize = minDimension / 9;

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
						mw.x, mw.y+pixelsize, mw.className, strlen(mw.className), mw.w);
					break;
				case 2:
					if (mw.name)
					drawUtfText(dpy, m->draws[mw.workspace], colorsCtx->wFonts, 
						colorsCtx->nFonts, colorsCtx->fontColor, 
						mw.x, mw.y+pixelsize, mw.name, strlen(mw.name), mw.w);
					break;
			}
		}
		ptr = ptr->next;
	}

	// Draw workspace labels
	for(i=0; i<nWorkspaces; ++i) {
		if (m->workspaceNames[i] != NULL) {
			drawUtfText(dpy, m->draws[i], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 5, m->sizing->previewHeight-10,
					m->workspaceNames[i], strlen(m->workspaceNames[i]), -1);
		}
	}

	// TODO: customize where search string is drawn
	// Draw search string after everything to ensure it's on top
	if (m->mode == 1) {
		int prefixLen = strlen(search->prefix);
		char sstring[search->size+prefixLen];
		strcpy(sstring, search->prefix);
		strcat(sstring,search->buffer);
		drawUtfText(dpy, m->draws[0], colorsCtx->fonts, colorsCtx->nFonts, colorsCtx->fontColor, 10,20+pixelsize,
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

void setDock(Display* dpy, Window w, XDConfig* cfg) {
	
	Atom cardinal = XInternAtom(dpy,"CARDINAL",False);
	unsigned long allDesktops = 0xFFFFFFFF;
	XChangeProperty(dpy, w,
			XInternAtom(dpy, "_NET_WM_DESKTOP", False),
			cardinal,
			32, PropModeReplace, (unsigned char*)&allDesktops, 1);


	Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(dpy, w,
			XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
			XInternAtom(dpy, "ATOM", False),
			32, PropModeReplace,
			(unsigned char*)&dock, 1);

	// TODO: configurable dock placement
	long insets[12] = {0};
	if (cfg->dockType){
		if (strcmp(cfg->dockType, "Bottom") == 0) {
			insets[3] = cfg->height;
			insets[10] = 0;
			insets[11] = 2559;
		} else if (strcmp(cfg->dockType, "Top") == 0) {
			insets[2] = cfg->height;
			insets[8] = 0;
			insets[9] = 2559;
		} else if (strcmp(cfg->dockType, "Left") == 0) {
			insets[0] = cfg->width;
			insets[4] = 0;
			insets[5] = 1439;
		} else if (strcmp(cfg->dockType, "Right") == 0) {
			insets[1] = cfg->width;
			insets[6] = 0;
			insets[7] = 1439;
		}
	}
	XChangeProperty(dpy, w,
			XInternAtom(dpy, "_NET_WM_STRUT", False),
			cardinal,
			32, PropModeReplace, (unsigned char*)insets, 4);

	XChangeProperty(dpy, w,
			XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False),
			cardinal,
			32, PropModeReplace, (unsigned char*)insets, 12);


}

int MARGIN = 2;
Window createMainWindow(Display *dpy, int screen, unsigned short nWorkspaces, unsigned short workspacesPerRow,
		XDConfig* cfg) {
	// This code was written with 16:9 2560x1440 monitors
	int xPos = cfg->x;
	int yPos = cfg->y;
	if (cfg->dockType){
		if (strcmp(cfg->dockType, "Bottom") == 0) {
			xPos = (2560 - cfg->width)/2;
			yPos = 1440 - cfg->height;
		} else if (strcmp(cfg->dockType, "Top") == 0) {
			xPos = (2560 - cfg->width)/2;
			yPos = 0;
		} else if (strcmp(cfg->dockType, "Left") == 0) {
			xPos = 0;
			yPos = (1440 - cfg->height)/2;
		} else if (strcmp(cfg->dockType, "Right") == 0) {
			xPos = 2560*2 - cfg->width;
			yPos = (1440 - cfg->height)/2;
		}
	}
	
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), xPos, yPos, cfg->width, cfg->height, 
			0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	// Set metadata on the window before mapping
	XClassHint *classHint = XAllocClassHint();
	classHint->res_name = "xdpager";
	classHint->res_class = "xdpager";
	XSetClassHint(dpy, win, classHint);
	XFree(classHint);

	setTitle(dpy, win);
	if (cfg->dockType)
		setDock(dpy, win, cfg);

	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(dpy, win);

	return win;
}

int getCurrentDesktop(Display* dpy) {
	Atom prop = XInternAtom(dpy,"_NET_CURRENT_DESKTOP",False);
	Atom cardinal = XInternAtom(dpy,"CARDINAL",False);
	Atom actualType;
	int format;
	unsigned long nitems;
	unsigned long bytesAfter;
	unsigned char* value;
	XGetWindowProperty(dpy, DefaultRootWindow(dpy), prop,
			0,100,False,cardinal,
			&actualType,&format,&nitems,&bytesAfter, &value);
	if (value) {
		int result = *(int*)value;
		XFree(value);
		return result;
	}
	return -1;
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

XftFont* initFont(Display* dpy, int screen, char* fontName, Sizing* sizing) {
	// Load Font to use
	int minDimension = sizing->previewHeight < sizing->previewWidth ? 
		sizing->previewHeight : sizing->previewWidth;
	int pixelsize = minDimension / 9;
	char fontWithSize[100*sizeof(char)];
	sprintf(fontWithSize, "%s:pixelsize=%d", fontName, pixelsize);
	XftFont* font = XftFontOpenName(dpy, screen, fontWithSize);
	if (!font) {
		printf("failed to open font %s\n", fontName);
		exit(1);
	}
	return font;
}

void reloadFonts(Model* model, Display* dpy, int screen) {
	GfxContext* ctx = model->gfx;

	// cleanup. No need to free since # of fonts doesn't change
	if (ctx->fonts) {
		for (int i=0; i<ctx->nFonts; i++) {
			XftFontClose(dpy, ctx->fonts[i]);
		}
	}

	Sizing* s = model->sizing;
	// Load Fonts
	// TODO: robustness, configurability, etc.
	int nFonts = 3;
	XftFont** fonts = malloc(nFonts * sizeof(XftFont*));
	fonts[0] = initFont(dpy, screen, "Font Awesome 6 Free Solid", s);
	fonts[1] = initFont(dpy, screen, "Font Awesome 6 Brands", s);
	fonts[2] = initFont(dpy, screen, "monospace", s);

	XftFont* windowFont = initFont(dpy, screen, "monospace", s);
	XftFont** wFonts = malloc(nFonts * sizeof(XftFont*));
	memcpy(wFonts,fonts,nFonts*sizeof(XftFont*));
	wFonts[2] = windowFont;

	ctx->fonts = fonts;
	ctx->nFonts = nFonts;
	ctx->wFonts = wFonts;
}

// Initializes everything we need for drawing to a Window/XftDraw
// TODO: might be able to ditch the xlib GCs in favor of using Xft lib's rectangle drawing
GfxContext* initColors(Display* dpy, int screen, XDConfig* cfg) {
	GfxContext* ctx = malloc(sizeof(GfxContext));
	ctx->pixels = malloc(3* sizeof(unsigned long));

	Visual* visual = DefaultVisual(dpy, screen);
	Colormap colormap = DefaultColormap(dpy,screen);
	XftColor* fontColor = malloc(sizeof(XftColor));
	if (!XftColorAllocName(dpy,visual,colormap,cfg->fontColor,fontColor)) {
		printf("Failed to allocate xft color %s\n", cfg->fontColor);
		exit(1);
	}
	ctx->fontColor = fontColor;

	// Set the background color of the selected window to something different
	XColor parsedColor;
	XParseColor(dpy, colormap, cfg->selectedColor, &parsedColor);
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
	XParseColor(dpy, colormap, cfg->desktopFg, &parsedColor);
	XAllocColor(dpy, colormap, &parsedColor);
	gcv.foreground = parsedColor.pixel; // Color used by fillRectangle
	gcv.background = WhitePixel(dpy,screen); // Irrelevant rn
	gc = XCreateGC(dpy, RootWindow(dpy, screen), GCForeground | GCBackground, &gcv);
	ctx->normal = gc;
	ctx->pixels[1] = parsedColor.pixel;

	// Workspace GC, used to draw the worksapce and outline floating windows
	XGCValues gcv_ws;
	GC gc_ws;
	XParseColor(dpy, colormap, cfg->desktopBg, &parsedColor);
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

// assumes preview window dimensions already set
void setPreviewScaling(Model* model) {
	int nRows = model->nWorkspaces / model->workspacesPerRow;
	if (model->nWorkspaces % model->workspacesPerRow != 0)
		nRows++;
	Sizing* s = model->sizing;
	s->s_x = 2560.0 /  s->previewWidth;
	s->s_y = 1440.0 / s->previewHeight;
	// printf("s_x s_y %f %f\n", s->s_x, s->s_y);
}

// If the main window has been resized, adjust the child windows aspect ratio,
// no matter how dumb it looks
void resizeWorkspaceWindows(Display* dpy, Model* m) {

	Sizing* s = m->sizing;
	Window* workspaces = m->workspaces;
	int nWorkspaces = m->nWorkspaces;
	int workspacesPerRow = m->workspacesPerRow;
	
	int nRows = nWorkspaces/workspacesPerRow;
	if (nWorkspaces % workspacesPerRow != 0)
		nRows += 1;
	int windowWidth = ((s->width - MARGIN) / workspacesPerRow ) - MARGIN;
	int windowHeight = ((s->height - MARGIN) / nRows) - MARGIN;
	for (int i=0; i<nWorkspaces; i++) {
		int xoff = i%workspacesPerRow;
		int yoff = i/workspacesPerRow;
		int x = (xoff * windowWidth) + ( (xoff+ 1) * MARGIN );
		int y = (yoff * windowHeight) + ( (yoff+1) * MARGIN);
		// printf("resize %d %d %d %d %d %d\n", x, y, windowWidth, windowHeight, s->width, s->height);
		XMoveResizeWindow(dpy, workspaces[i], x, y, windowWidth, windowHeight);
	}

	s->previewWidth = windowWidth;
	s->previewHeight = windowHeight;
}

void handleResize(Display* dpy, int screen, Model* model) {
	resizeWorkspaceWindows(dpy, model);
	setPreviewScaling(model);
	cleanupList(model->previews);
	model->previews = testX(dpy, model->sizing->s_x, model->sizing->s_y);
	reloadFonts(model, dpy, screen);
//	printf("w,h %d,%d\n", model->sizing->width, model->sizing->height);
}

// Handle a keypress in the workspace mode
// returns whether or not we should exit afterwards
int workspaceKey(KeySym sym, Model* model, Display* dpy, int screen, Window wMain) {

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
	} else if (sym == XK_F3) {
		if (model->workspacesPerRow < model->nWorkspaces) {
			model->workspacesPerRow++;
			handleResize(dpy, screen, model);
		}
	} else if (sym == XK_F4) {
		if (model->workspacesPerRow > 1) {
			model->workspacesPerRow--;
			handleResize(dpy, screen, model);
		}
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


char isWorkspaceWindow(Window* workspaces, int nWorkspaces, Window w) {
	for (int i=0; i<nWorkspaces; i++) {
		if (workspaces[i] == w)
			return 1;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	XDConfig* cfg = parseArgs(argc,argv);
	if (cfg->navType)
		navType = cfg->navType;
	if (cfg->margin)
		MARGIN = cfg->margin;

	XSetErrorHandler(errorHandler);
	Display *dpy;
	int screen;
	Visual* visual;
	Window win;
	XEvent event;

	// Config options
	unsigned short nWorkspaces = 9;      // Number of workspaces/desktops
	unsigned short workspacesPerRow =9;
	unsigned short maxWindows = 30;      // Maximum number of windows to preview (TODO: dynamic arrays)

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

	// Get the desktop we're currently on
	// TODO: Xinerama to determine where we actually are
	int currentDesktop = getCurrentDesktop(dpy);

	int width_old = cfg->width;
	int height_old = cfg->height;
	win = createMainWindow(dpy,screen, nWorkspaces, workspacesPerRow, cfg);
	
	// TODO colors as configureable options.  Formatting
	GfxContext* colorsCtx = initColors(dpy, screen, cfg);

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

	// Size/scale info
	Sizing* s = malloc(sizeof(Sizing));
	s->width = width_old;
	s->height = height_old;
	s->s_x = s_x;
	s->s_y = s_y;
	s->previewWidth = width;
	s->previewHeight = height;	

	// Collect all this shit together for organization
	Model* model = malloc(sizeof(Model));
	model->workspaces = workspaces;
	model->workspaceNames = workspaceNames;
	model->draws = draws;
	model->nWorkspaces = nWorkspaces;
	model->previews = miniWindows;
	model->selected = currentDesktop;
	model->search = search;
	model->mainWindow = workspaces[0];
	model->mode = 0;
	model->windowTextMode = 0;
	model->workspacesPerRow = workspacesPerRow;
	model->sizing = s;
	model->gfx = colorsCtx;

	reloadFonts(model, dpy, screen);
	handleResize(dpy, screen, model);

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
			if (event.xconfigure.window == win) {
				// If our window has been resized, update the scale factors
				int w = event.xconfigure.width;
				int h = event.xconfigure.height; 
				if (model->sizing->width != w || model->sizing->height != h) {
					model->sizing->width = w;
					model->sizing->height = h;
					handleResize(dpy, screen, model);
				} else {
					cleanupList(model->previews);
					model->previews = testX(dpy, model->sizing->s_x, model->sizing->s_y);
				}
			} else if (isWorkspaceWindow(workspaces, nWorkspaces, event.xconfigure.window)) {
				printf("notify on child window 0x%lx\n", event.xconfigure.window);
			} else {
				// Some other window has changed size
				// Don't need to update our layout or scaling, just the list of previews
				cleanupList(model->previews);
				model->previews = testX(dpy, model->sizing->s_x, model->sizing->s_y);
			}
			redraw(dpy,screen,MARGIN,colorsCtx,model);
		}

		// Expose events
		// Redraw fully only on the last damaged event
		if (event.type == Expose && event.xexpose.count == 0) {
			//printf("Expose event for %lx\n", event.xexpose.window);
			redraw(dpy,screen,MARGIN,colorsCtx,model);
			if (event.xany.window == win && navType == NAV_MOVE_WITH_SELECTION) {
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
					shouldExit = workspaceKey(sym, model, dpy, screen, win);
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
				redraw(dpy,screen,MARGIN,colorsCtx,model);
				if (navType == NAV_MOVE_WITH_SELECTION) {
					grabFocus(win);
				}
			}
		}
		
		// Mouse movement
		// Mouse selection of filtered windows not implemented. Would need to do geometry range checking
		if (event.type == MotionNotify && model->mode == 0) {
			int pWorkspace = findPointerWorkspace(event.xmotion.window, workspaces, nWorkspaces);
			if (pWorkspace != model->selected){
				model->selected = pWorkspace;
				redraw(dpy,screen,MARGIN,colorsCtx,model);
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
	if (cfg->searchPrefix)
		free(cfg->searchPrefix);
	if (cfg->dockType)
		free(cfg->dockType);
	//if (cfg->desktopFg)
	//	free(cfg->desktopFg);
	//if (cfg->desktopBg)
	//	free(cfg->desktopBg);
	//if (cfg->selectedColor)
	//	free(cfg->selectedColor);
	//if (cfg->fontColor)
	//	free(cfg->fontColor);
	free(cfg);
	return 0;
}
