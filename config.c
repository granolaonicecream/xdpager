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
	char* font;
	char* windowFont;
} XDConfig;

XDConfig* defaultConfig() {
	XDConfig* cfg = malloc(sizeof(XDConfig));
	cfg->x = 0;
	cfg->y = 0;
	cfg->width = 400;
	cfg->height = 300;
	cfg->margin = 2;
	cfg->desktopFg = "#4d5257";
	cfg->desktopBg = "#1d1f21";
	cfg->selectedColor = "#f2e750";
	cfg->fontColor = "#cfc542";
	cfg->navType = 1;
	cfg->nDesktops = 9;
	cfg->desktopsPerRow = 3;
	cfg->font= "Font Awesome 6 Free Solid,Font Awesome 6 Brands,monospace";
	cfg->windowFont = NULL;

	return cfg;
}

void parseArgs(int argc, char* argv[], XDConfig* cfg) {
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
			{"nDesktops", required_argument, 0, 5},
			{"desktopsPerRow", required_argument, 0, 6},
			{"font", required_argument, 0, 7},
			{"windowFont", required_argument, 0, 8},

		};
		int opt_idx = 0;
		int c = getopt_long(argc, argv, "c:d:x:y:w:h:t:m:", long_options, &opt_idx);
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
			case 5:
				cfg->nDesktops = strtoul(optarg, NULL, 10);
				break;
			case 6:
				cfg->desktopsPerRow = strtoul(optarg, NULL, 10);
				break;
			case 7:
				cfg->font = malloc(strlen(optarg));
				strcpy(cfg->font, optarg);
				break;
			case 8:
				cfg->windowFont = malloc(strlen(optarg));
				strcpy(cfg->windowFont, optarg);
				break;
			case 'c':
				// config file determined elsewhere
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
}

// Assume config file lives in $HOME/.config/xdpager/xdpagerrc
int parseline(char* line, XDConfig* config) {
	char key[100];
	char* delim = "=\n";
	char* token;

	// empty line or comment
	if (sscanf(line, " %s", key) == EOF)
		return 0;
	if (sscanf(line, " %[#]", key))
		return 0;

	token = strtok(line, delim);
	if (token != NULL) {
		strcpy(key,token);
		token = strtok(NULL, delim);
		//printf("%s,%s\n", key, token);

		if (strcmp(key, "desktopFg") == 0) {
			config->desktopFg = malloc(sizeof(char) * strlen(token));
			strcpy(config->desktopFg, token);
		} else if (strcmp(key, "desktopBg") == 0) {
			config->desktopBg = malloc(sizeof(char) * strlen(token));
			strcpy(config->desktopBg, token);
		} else if (strcmp(key, "selectedColor") == 0) {
			config->selectedColor = malloc(sizeof(char) * strlen(token));
			strcpy(config->selectedColor, token);
		} else if (strcmp(key, "fontColor") == 0) {
			config->fontColor= malloc(sizeof(char) * strlen(token));
			strcpy(config->fontColor, token);
		} else if (strcmp(key, "font") == 0) {
			config->font = malloc(sizeof(char) * strlen(token));
			strcpy(config->font, token);
		} else if (strcmp(key, "windowFont") == 0) {
			config->windowFont = malloc(sizeof(char) * strlen(token));
			strcpy(config->windowFont, token);
		} else if (strcmp(key, "nDesktops") == 0) {
			config->nDesktops = strtoul(token, NULL, 10);
		} else if (strcmp(key, "desktopsPerRow") == 0) {
			config->desktopsPerRow = strtoul(token, NULL, 10);
		} else if (strcmp(key, "xPos") == 0) {
			config->x = strtoul(token, NULL, 10);
		} else if (strcmp(key, "yPos") == 0) {
			config->y = strtoul(token, NULL, 10);
		} else if (strcmp(key, "width") == 0) {
			config->width = strtoul(token, NULL, 10);
		} else if (strcmp(key, "height") == 0) {
			config->height = strtoul(token, NULL, 10);
		} else if (strcmp(key, "margin") == 0) {
			config->margin = strtoul(token, NULL, 10);
		} else if (strcmp(key, "navType") == 0) {
			config->navType = strtoul(token, NULL, 10);
		} else if (strcmp(key, "searchPrefix") == 0) {
			config->searchPrefix = malloc(sizeof(char) * strlen(token));
			strcpy(config->searchPrefix, token);
		} else {
			printf("Unrecognized property %s, skipping\n", key);
		}
		return 0;
		
	}

	return 1;
}

void parseConfigFile(XDConfig* config, char* path) {
	FILE *f = fopen(path, "r");
	char line[256];

	while (fgets(line, 256, f)) {
		parseline(line, config);
	}
}

XDConfig* getConfig(int argc, char* argv[]) {
	// get default configuration
	XDConfig* cfg = defaultConfig();
	// get config from file
	// If provided with path, use it.  Otherwise use $XDG_CONFIG_DIR
	// yes, yes, O(n)
	char* prefix = getenv("XDG_CONFIG_HOME");
	if (prefix == NULL) {
		prefix = getenv("HOME");
	}
	char path[200];
        sprintf(path, "%s/.config/xdpager/xdpager-rc", prefix);
	for (int i=0; i<argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			sprintf(path, "%s", argv[i+1]);
			break;	
		}
	}
//	printf("path = %s\n", path);
	parseConfigFile(cfg, path);
	
	// parse any remaining command line args
	parseArgs(argc, argv, cfg);

	return cfg;
}
