#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

typedef struct {
	unsigned int x,y,width,height;
	char* dockType;
	char* searchPrefix;
	unsigned short nDesktops;
	unsigned short desktopsPerRow;
	unsigned int margin;
	unsigned int navType;
	char* desktopFg;
	char* desktopBg;
	char* selectedColor;
	char* fontColor;
} XDConfig;

XDConfig* defaultConfig() {
	XDConfig* cfg = malloc(sizeof(XDConfig));
	cfg->desktopFg = "#4d5257";
	cfg->desktopBg = "#1d1f21";
	cfg->selectedColor = "#f2e750";
	cfg->fontColor = "#f2e750";
	cfg->navType = 1;

	return cfg;
}

XDConfig* parseArgs(int argc, char* argv[]) {
	XDConfig* cfg = defaultConfig();
	static int verbose_flag;
	while(1) {
		static struct option long_options[] = {
			{"verbose", no_argument, &verbose_flag, 1},
			{"dock", required_argument, 0, 'd'},
			{"xPos", required_argument, 0, 'x' },
			{"yPos", required_argument, 0, 'y' },
			{"width", required_argument, 0, 'w'},
			{"height", required_argument, 0, 'h'},
			{"margin", required_argument, 0, 'm'},
			{"navType", required_argument, 0, 't'},
			{"searchPrefix", required_argument, 0, 's'},
			{"desktopFg", required_argument, 0, 1},
			{"desktopBg", required_argument, 0, 2},
			{"selectedColor", required_argument, 0, 3},
			{"fontColor", required_argument, 0, 4},

		};
		int opt_idx = 0;
		int c = getopt_long(argc, argv, "d:x:y:w:h:t:m:", long_options, &opt_idx);
		if (c == -1)
			break;
		switch(c) {
			case 0:
				if (long_options[opt_idx].flag != 0) {
					break;
				}
				break;
			case 1:
				cfg->desktopFg = malloc(strlen(optarg));
				strcpy(cfg->desktopFg, optarg);
				break;
			case 2:
				cfg->desktopBg = malloc(strlen(optarg));
				strcpy(cfg->desktopBg, optarg);
				break;
			case 3:
				cfg->selectedColor = malloc(strlen(optarg));
				strcpy(cfg->selectedColor, optarg);
				break;
			case 4:
				cfg->fontColor = malloc(strlen(optarg));
				strcpy(cfg->fontColor, optarg);
				break;
			case 'd':
				cfg->dockType = malloc(sizeof(char)*10);
				strcpy(cfg->dockType, optarg);
				break;
			case 'x':
				cfg->x = strtoul(optarg, NULL, 10);
				break;
			case 'y':
				cfg->y = strtoul(optarg, NULL, 10);
				break;
			case 'w':
				cfg->width = strtoul(optarg, NULL, 10);
				break;
			case 'h':
				cfg->height = strtoul(optarg, NULL, 10);
				break;
			case 'm':
				cfg->margin = strtoul(optarg, NULL, 10);
				break;
			case 't':
				cfg->navType = strtoul(optarg, NULL, 10);
				break;
			case 's':
				cfg->searchPrefix = malloc(sizeof(char)*10);
				strcpy(cfg->searchPrefix, optarg);
				break;
			default:
				break;
		}

	}
	return cfg;
}