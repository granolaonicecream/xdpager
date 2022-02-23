#include <X11/extensions/Xinerama.h>

typedef struct {
	int x_offset;
	int y_offset;
	int width;
	int height;
} Monitor;

llist* getMonitors(Display* dpy) {
	int nMonitors;
	XineramaScreenInfo* screens = XineramaQueryScreens(dpy, &nMonitors);
	llist* list = llist_create();
	for (int i=0; i<nMonitors; i++) {
		printf("monitor %d+%d %dx%d\n", screens[i].x_org, screens[i].y_org, screens[i].width, screens[i].height);
		Monitor* mon = malloc(sizeof(Monitor));
	       	mon->x_offset = screens[i].x_org;
		mon->y_offset = screens[i].y_org;
		mon->width = screens[i].width;
		mon->height = screens[i].height;
		llist_addBack(list, mon);
	}

	XFree(screens);

	return list;
}

