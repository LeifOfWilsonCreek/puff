/*
bugs:
	ENODEV doesn't make sense on some systems
	Resizing the screen can leave the current line out of sight
	Cutting should "paste" into the clipboard so the window moves properly
	Setting lock_files=0 in puff.conf makes puff hang

to-do:
	+20 cmd-line option to go to line 20
	top/bottom of file/screen
	next-word
	insert file
	undo
	kill marked piece
	copy line with reverse color flash
	[key-file] section of config file for file menu
	search-all-files, s & replace, regex search, case insensitive
	show mods in another color
	cmd to pipe the marked text to a program such as lpr
	auto-indent
	center, justify {line,para} {left/center/right/full/all}
	word wrap with configurable right margin
	save-changes timer
	new-line fix
	backup old file option
	workspace saver to open files that were previously open
	half-page{up,down}
*/

#include <ctype.h>
#include <curses.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "editfile.h"
#include "keys.h"
#include "shared.h"

#define MAX_COLS        1023    /* max number of columns on a terminal */
#define PUFF_MAX_FILES    36    /* max number of files that we can open, including clipboard */

/* These macros are a little funky.  Normally the "curfile" they reference is
 * the global variable called "curfile", which represents what file the user is
 * currently looking at.  Some functions have a parameter called "curfile".  In
 * these, the macros reference that parameter.  This is because occasionally we
 * have to do some work with a file that is not currently shown, such as when
 * cutting to the clipboard.  This probably isn't "good practice", but I found
 * it easier in this case. */

#define NAME        finfo[curfile].name
#define FP          finfo[curfile].fp
#define WIN         finfo[curfile].win
#define OFFSET      finfo[curfile].offset
#define HSIZE       finfo[curfile].hsize
#define VSIZE       finfo[curfile].vsize
#define LEFTCOL     finfo[curfile].leftcol
#define TOPLINE     finfo[curfile].topline
#define HCURSOR     finfo[curfile].hcursor
#define VCURSOR     finfo[curfile].vcursor
#define REALH       finfo[curfile].realh
#define MODIFIED    finfo[curfile].modified

/* Global variables */
struct {
	char name[MAXPATHLEN];		/* name of file */
	EDITFILE *fp;			/* underlying file stream */
	WINDOW *win;			/* associated window */
	off_t offset;			/* offset of cursor position */
	int vsize, hsize;		/* size of window -- NOT terminal */
	int topline, leftcol;		/* what line/col the window begins on */
	int vcursor; off_t hcursor;	/* cursor screen position, 0-indexed */
	off_t realh;			/* the real cursor position for the line */
	int modified;			/* true if the file was modified */
} finfo[PUFF_MAX_FILES];
int ((**funcarray)());
extern char *optarg;
extern int optind;
int argc;
char **argv;
WINDOW *statuswin, *messagewin = 0;
int curfile;				/* -1 == no current file, 0 == clipboard */
char homedir[MAXPATHLEN] = "";
int color_term;
int tab_size;
int page_skip;
int save_clipboard;
int lock_files;
int window_margin;	/* The minimum number of lines to keep on the screen above and below the cursor */
int seek_centers;

/* Some function prototypes that can't be declared before they are used. */
int keyleft();
int keyright();

void redraw_statuswin(char *fmt, ...) {
	static int oldstart = 0;
	static time_t age = 0;
	char tmpstr[MAX_COLS];
	char tmpchar;
	va_list args;
	int max_x, max_y, tmpint;

	getmaxyx(statuswin, max_y, max_x);
	if(!fmt && difftime(time(NULL), age) > 5) {
		fmt = "file";
		if(curfile != -1 && FP) file_cleanup(FP);
	}
	if(fmt) {
		werase(statuswin);
		time(&age);
	}

	if(curfile >= 0) {
		wmove(statuswin, 0, oldstart);
		wclrtoeol(statuswin);
		if(curfile < 10) {
			tmpchar = curfile + '0';
		} else if(curfile < 36) {
			tmpchar = curfile + 'A' - 10;
		} else {
			/* Shouldn't happen unless PUFF_MAX_FILES gets modified. */
			tmpchar = '!';
		}
		snprintf(tmpstr, sizeof(tmpstr), " %c %i/%i %i/%i %i %n        ", tmpchar, (int)OFFSET, (int)file_size(FP), TOPLINE + VCURSOR + 1, file_numlines(FP), (int)REALH + 1, &tmpint);
		if(file_size(finfo[0].fp)) tmpstr[tmpint] = 'C', tmpstr[tmpint+1] = 'B';
		if(MODIFIED) tmpstr[tmpint+5] = 'M', tmpstr[tmpint+6] = 'o', tmpstr[tmpint+7] = 'd';
		if(file_fileno(FP) == -1) tmpstr[tmpint+5] = 'N', tmpstr[tmpint+6] = 'e', tmpstr[tmpint+7] = 'w';
		else if(!file_writable(FP)) tmpstr[tmpint+3] = 'R';

		oldstart = max_x - strlen(tmpstr) - 1;
		wmove(statuswin, 0, oldstart);
		waddstr(statuswin, tmpstr);
	}
	wmove(statuswin, 0, 0);
	if(!fmt) {
		redrawwin(statuswin);
		wrefresh(statuswin);
		return;
	}

	if(!strcmp("file", fmt)) {
		if(curfile == -1) {
			waddstr(statuswin, "No open files.");
		} else {
			if(*NAME) {
				if(strlen(NAME) > oldstart + 2) {
					strcpy(tmpstr, "...");
					strcpy(tmpstr + 3, NAME + strlen(NAME) - oldstart + 4);
				} else {
					strcpy(tmpstr, NAME);
				}

				waddstr(statuswin, tmpstr);
			} else {
				waddstr(statuswin, "/* New file */");
			}
			if(!strcmp(NAME, "puff.c")) waddstr(statuswin, " - Hey, that's me! :)");
		}
		redrawwin(statuswin);
		wrefresh(statuswin);
		return;
	}

	va_start(args, fmt);
	vwprintw(statuswin, fmt, args);
	va_end(args);

	redrawwin(statuswin);
	wrefresh(statuswin);
	return;
}

/* redisplay a line.  If term_line == -1, redisplay the current line.
 * We start by knowing TOPLINE, [VH]CURSOR, [VH]SIZE and a tenative LEFTCOL,
 * and calculate OFFSET. */
void display_line(int curfile, int term_line) {
	int vcurs, tab, tmpint, storepos, carryover = 0;
	off_t hcurs, leftcol, pos, off;

	if(term_line == -1) {
		vcurs = VCURSOR;
		hcurs = HCURSOR;
		/* leftcol sanity - it must land on a tab */
		LEFTCOL -= LEFTCOL % tab_size;
		while(LEFTCOL + HSIZE <= hcurs)
			LEFTCOL += HSIZE - HSIZE % tab_size;
		while(LEFTCOL > hcurs)
			LEFTCOL -= HSIZE - HSIZE % tab_size;
		if(LEFTCOL < 0) LEFTCOL = 0;
		leftcol = LEFTCOL;
	} else {
		vcurs = term_line;
		hcurs = 0;
		leftcol = 0;
	}
	wmove(WIN, vcurs, 0);
	wclrtoeol(WIN);

	tab = tab_size;
	if((off = file_findline(FP, TOPLINE + vcurs)) < 0) return;
	pos = 0;
	while(pos < leftcol) {
		tmpint = file_getc(FP, off);
		if(tmpint == '\t') {
			pos += tab;
			tab = tab_size;
			off++;
		} else if(tmpint == '\n' || tmpint == -1) {
			/* line is too short - start over */
			leftcol -= HSIZE - HSIZE % tab_size;
			if(leftcol < 0) leftcol = 0;
			LEFTCOL = leftcol;	/* this loop only happens on the current line */
			tab = tab_size;
			off = file_findline(FP, TOPLINE + vcurs);
			pos = 0;
		} else if(tmpint < 32 || tmpint == 127) {
			if(!--tab) tab = tab_size;
			if(!--tab) tab = tab_size;
			pos += 2;
			off++;
			if(pos == leftcol + 1) carryover = (tmpint == 127 ? '?' : tmpint + 64);
		} else {
			if(!--tab) tab = tab_size;
			pos++;
			off++;
		}
	}
	/* pos now corresponds with off for the first char to display */

	storepos = pos = 0;
	if(carryover) {
		waddch(WIN, carryover);
		if(!--tab) tab = tab_size;
		pos++;
	}
	while(pos < HSIZE) {
		if(pos + leftcol <= hcurs) {
			if(term_line == -1) OFFSET = off;
			storepos = pos;
		}
		tmpint = file_getc(FP, off);
		if(tmpint == '\t') {
			pos += tab;
			if(pos >= HSIZE) break;
			while(tab--) waddch(WIN, ' ');
			tab = tab_size;
			off++;
		} else if(tmpint == '\n' || tmpint == -1) {
			/* EOL or EOF */
			break;
		} else if(tmpint < 32 || tmpint == 127) {
			/* Backspace and Carriage return mean special things to curses, and
			 * it might run off the end of the world, so we gotta do this our
			 * own way */
			waddch(WIN, '^');
			if(!--tab) tab = tab_size;
			pos++;
			if(pos < HSIZE) {
				if(tmpint == 127) {
					waddch(WIN, '?');
				} else {
					waddch(WIN, tmpint + 64);
				}
				if(!--tab) tab = tab_size;
				pos++;
			}
			off++;
		} else {
			waddch(WIN, tmpint);
			if(!--tab) tab = tab_size;
			pos++;
			off++;
		}
	}
	if(term_line == -1) {
		REALH = leftcol + storepos;
		wmove(WIN, vcurs, storepos);
	}
	return;
}

/* redisplay the whole file */
void display_file(int curfile) {
	int row;
	for(row = 0; row < VSIZE; row++) display_line(curfile, row);
	return;
}

int newfile(int curfile, char *filename) {
	if(filename) strlcpy(NAME, filename, sizeof(NAME));
	else *NAME = 0;
	if(!(FP = file_new())) return ERR;
	OFFSET = 0;
	VSIZE = LINES-1;
	HSIZE = COLS;
	TOPLINE = 0;
	LEFTCOL = 0;
	VCURSOR = 0;
	HCURSOR = 0;
	REALH = 0;
	MODIFIED = 0;

	if(!(WIN = newwin(VSIZE, HSIZE, 0, 0))) {
		file_close(FP);
		FP = 0;
		redraw_statuswin("Could not make edit window");
		return ERR;
	}
	idlok(WIN, TRUE);

	display_file(curfile);
	return OK;
}

int openfile(int curfile, char *filename) {
	struct stat new_stat;
	int tmpint;
	char tmpstr[MAXPATHLEN], tmpchar;

	strcpy(NAME, filename);

	if(stat(filename, &new_stat)) {
		redraw_statuswin("%s:  %s", filename, strerror(errno));
		return ERR;
	}

	/* Make sure the file isn't already open */
	for(tmpint = 0; tmpint < PUFF_MAX_FILES; tmpint++) {
		if(file_fileno(finfo[tmpint].fp) == -1) continue;

		if(file_stat(finfo[tmpint].fp) && new_stat.st_dev == file_stat(finfo[tmpint].fp)->st_dev && new_stat.st_ino == file_stat(finfo[tmpint].fp)->st_ino) {
			redraw_statuswin("File already open as number %i.", tmpint);
			return ERR;
		}
	}

	FP = file_open(filename, O_RDWR|(lock_files?FILE_LOCK:0), 0);
	if(!FP && errno != EAGAIN) {
		/* Only happens if the filesystem's locking is broken */
		FP = file_open(filename, O_RDWR, 0);
		if(FP) {
			redraw_statuswin("Warning - Could not lock file: %s", strerror(errno));
			sleep(1);
		}
	}
	if(!FP) {
		FP = file_open(filename, O_RDONLY|(lock_files?FILE_LOCK:0), 0);
	}
	if(!FP && errno != EAGAIN) {
		/* Only happens if the filesystem's locking is broken */
		FP = file_open(filename, O_RDONLY, 0);
		if(FP) {
			redraw_statuswin("Warning - Could not lock file: %s", strerror(errno));
			sleep(1);
		}
	}
	if(!FP) {
		redraw_statuswin("%s:  %s", filename, strerror(errno));
		return ERR;
	}

	OFFSET = 0;
	VSIZE = LINES-1;
	HSIZE = COLS;
	TOPLINE = 0;
	LEFTCOL = 0;
	VCURSOR = 0;
	HCURSOR = 0;
	REALH = 0;
	MODIFIED = 0;

	if(!(WIN = newwin(VSIZE, HSIZE, 0, 0))) {
		file_close(FP);
		FP = 0;
		redraw_statuswin("Could not make edit window");
		return ERR;
	}
	idlok(WIN, TRUE);

	display_file(curfile);
	redraw_statuswin("file");

	if(snprintf(tmpstr, sizeof(tmpstr), "%s.changes", NAME) >= sizeof(tmpstr)) return OK;
	if(access(tmpstr, R_OK)) return OK;  /* No changes file - so just return. */

	if(stat(tmpstr, &new_stat)) return OK;  /* Shouldn't happen, tho */

	if(new_stat.st_mtime < file_stat(FP)->st_mtime) {
		redraw_statuswin("Load interrupted modifications (Warning - mods are older than file)?  ");
	} else {
		redraw_statuswin("Load interrupted modifications?  ");
	}

	tmpchar = 0;
	if(kgetstr(statuswin, &tmpchar, 1, "YyNn") == OK && (tmpchar == 'Y' || tmpchar == 'y')) {
		redraw_statuswin("Restoring...");
		if(file_restore(FP, tmpstr)) {
			redraw_statuswin("Failed!");
			return OK;
		}
		MODIFIED = 1;
		display_file(curfile);
	}

	redraw_statuswin("Remove %s?  ", tmpstr);
	tmpchar = 0;
	if(kgetstr(statuswin, &tmpchar, 1, "YyNn") == OK && (tmpchar == 'Y' || tmpchar == 'y')) {
		unlink(tmpstr);
	}

	redraw_statuswin("file");
	return OK;
}

/* Converts mode_t to a nice string stored in buf */
void text_perms(char *buf, mode_t mode) {
	if(S_ISREG(mode))
		buf[0] = '-';
	else if(S_ISDIR(mode))
		buf[0] = 'd';
	else if(S_ISFIFO(mode))
		buf[0] = 'p';
	else if(S_ISCHR(mode))
		buf[0] = 'c';
	else if(S_ISBLK(mode))
		buf[0] = 'b';
	else if(S_ISLNK(mode))
		buf[0] = 'l';
	else if(S_ISSOCK(mode))
		buf[0] = 's';
	else
		buf[0] = '?';
	if(mode&S_IRUSR) buf[1] = 'r'; else buf[1] = '-';
	if(mode&S_IWUSR) buf[2] = 'w'; else buf[2] = '-';
	if(mode&S_IXUSR) buf[3] = 'x'; else buf[3] = '-';
	if(mode&S_ISUID) buf[3] = mode&S_IXUSR ? 's' : 'S';
	if(mode&S_IRGRP) buf[4] = 'r'; else buf[4] = '-';
	if(mode&S_IWGRP) buf[5] = 'w'; else buf[5] = '-';
	if(mode&S_IXGRP) buf[6] = 'x'; else buf[6] = '-';
	if(mode&S_ISGID) buf[6] = mode&S_IXGRP ? 's' : 'S';
	if(mode&S_IROTH) buf[7] = 'r'; else buf[7] = '-';
	if(mode&S_IWOTH) buf[8] = 'w'; else buf[8] = '-';
	if(mode&S_IXOTH) buf[9] = 'x'; else buf[9] = '-';
	if(mode&S_ISVTX) buf[9] = mode&S_IXOTH ? 't' : 'T';
	buf[10] = 0;
}

/* kills comments and leading/trailing whitespace */
void strip_comment(char *string) {
	int in_quotes = 0, tmp;
	char *ptr;

	ptr = string;

	/* Kill leading whitespace */
	tmp = strspn(ptr, "\n\r\t ");
	memmove(ptr + tmp, ptr, strlen(ptr + tmp));

	while(*ptr) {
		if(*ptr == '#' && !in_quotes) {
			*ptr = 0;
			return;
		} else if((in_quotes == '\'' && *ptr == '\'')
				|| (in_quotes == '"' && *ptr == '"')) {
			in_quotes = 0;
		} else if((!in_quotes && *ptr == '\'')
				|| (!in_quotes && *ptr == '"')) {
			in_quotes = *ptr;
		}
		ptr++;
	}

	/* ptr is now at the '\0' of the string.  Kill trailing whitespace. */
	while(--ptr >= string && strchr("\n\r\t ", *ptr)) *ptr = 0;
	return;
}

void configure_file(char *filename) {
	char keyword[128], *ptr, *nextptr, tmpstr[1024];
	int nextchar, index;
	FILE *file;

	if(!(file = fopen(filename, "r"))) return;
	fgets(tmpstr, sizeof(tmpstr), file);
	while(!feof(file) && !ferror(file)) {
		ptr = tmpstr;
		strip_comment(ptr);
		if(!strncasecmp(ptr, "[keys]", 6)) {
			definedkeys = 0;
			while(fgets(tmpstr, sizeof(tmpstr), file)) {	/* loop to fill keys */
				if(definedkeys + 1 >= sizeof(keys) / sizeof(keys[0])) break;
				strip_comment(tmpstr);
				if(*tmpstr == '[') break;	/* End of section */
				strlcpy(keyword, tmpstr, sizeof(keyword));
				ptr = strchr(keyword, '=');
				if(!ptr) continue;	/* keyword without value */
				*ptr = 0;
				strip_comment(keyword);	/* get rid of whitespace */
				nextchar = 0;
				if(!strcasecmp(keyword, "UP")) {
					nextchar = KUP;
				} else if(!strcasecmp(keyword, "DOWN")) {
					nextchar = KDOWN;
				} else if(!strcasecmp(keyword, "RIGHT")) {
					nextchar = KRIGHT;
				} else if(!strcasecmp(keyword, "LEFT")) {
					nextchar = KLEFT;
				} else if(!strcasecmp(keyword, "BOL")) {
					nextchar = KBOL;
				} else if(!strcasecmp(keyword, "EOL")) {
					nextchar = KEOL;
				} else if(!strcasecmp(keyword, "PPAGE")) {
					nextchar = KPPAGE;
				} else if(!strcasecmp(keyword, "NPAGE")) {
					nextchar = KNPAGE;
				} else if(!strcasecmp(keyword, "BACKSPACE")) {
					nextchar = KBACKSPACE;
				} else if(!strcasecmp(keyword, "INSERT")) {
					nextchar = KINSERT;
				} else if(!strcasecmp(keyword, "DELETE")) {
					nextchar = KDELETE;
				} else if(!strcasecmp(keyword, "SEEKBYTE")) {
					nextchar = KSEEKBYTE;
				} else if(!strcasecmp(keyword, "CANCEL")) {
					nextchar = KCANCEL;
				} else if(!strcasecmp(keyword, "SWITCH")) {
					nextchar = KSWITCH;
				} else if(!strcasecmp(keyword, "TAB")) {
					nextchar = KTAB;
				} else if(!strcasecmp(keyword, "ENTER")) {
					nextchar = KENTER;
				} else if(!strcasecmp(keyword, "CUT")) {
					nextchar = KCUT;
				} else if(!strcasecmp(keyword, "REDRAW")) {
					nextchar = KREDRAW;
				} else if(!strcasecmp(keyword, "CLEARCLIP")) {
					nextchar = KCLEARCLIP;
				} else if(!strcasecmp(keyword, "OPEN")) {
					nextchar = KOPEN;
				} else if(!strcasecmp(keyword, "PASTE")) {
					nextchar = KPASTE;
				} else if(!strcasecmp(keyword, "SAVE")) {
					nextchar = KSAVE;
				} else if(!strcasecmp(keyword, "SEEKLINE")) {
					nextchar = KSEEKLINE;
				} else if(!strcasecmp(keyword, "KILL")) {
					nextchar = KKILL;
				} else if(!strcasecmp(keyword, "KILLBOL")) {
					nextchar = KKILLBOL;
				} else if(!strcasecmp(keyword, "KILLEOL")) {
					nextchar = KKILLEOL;
				} else if(!strcasecmp(keyword, "SPECIAL")) {
					nextchar = KSPECIAL;
				} else if(!strcasecmp(keyword, "SEARCH")) {
					nextchar = KSEARCH;
				} else if(!strcasecmp(keyword, "EXIT")) {
					nextchar = KEXIT;
				} else if(!strcasecmp(keyword, "NEW")) {
					nextchar = KNEW;
				} else if(!strcasecmp(keyword, "CLOSE")) {
					nextchar = KCLOSE;
				} else if(!strcasecmp(keyword, "FILEMENU")) {
					nextchar = KFILEMENU;
				} else if(!strcasecmp(keyword, "SUSPEND")) {
					nextchar = KSUSPEND;
				} else if(!strcasecmp(keyword, "QSAVE")) {
					nextchar = KQSAVE;
				}
				if(!nextchar) continue;	/* Unknown keyword */
				keys[definedkeys][0] = nextchar;

				nextptr = strchr(tmpstr, '=') + 1;	/* error checked above */
				index = 1;
				while(*nextptr) {	/* loop to describe a keystroke */

					/* similar to strsep(), but takes ' and " into account */
					int in_quotes = 0;
					ptr = nextptr;
					while(*nextptr) {
						if(!in_quotes && *nextptr == ',') {
							*nextptr++ = 0;
							break;
						}
						if((*nextptr == '\'' && in_quotes == '\'') ||
							(*nextptr == '"' && in_quotes == '"')) {
							in_quotes = 0;
						} else if(!in_quotes && (*nextptr == '\'' || *nextptr == '"'))
							in_quotes = *nextptr;
						nextptr++;
					}
					/* ptr is the parameter to deal with...nextptr is the
					 * beginning of the next one. */

					strip_comment(ptr);	/* strip whitespace */

					if(*ptr >= '0' && *ptr <= '9') {
						/* direct ASCII value */
						keys[definedkeys][index++] = strtol(ptr, 0, 0);
					} else if(*ptr == '\'' || *ptr == '"') {
						/* literal string */
						keys[definedkeys][index++] = *++ptr;
					} else if(!strcasecmp(ptr, "ESC")) {
						keys[definedkeys][index++] = 27;
					} else if(!strcasecmp(ptr, "CTRL_SPACE")) {
						keys[definedkeys][index++] = 0;
					} else if(!strcasecmp(ptr, "NULL")) {
						keys[definedkeys][index++] = 0;
					} else if(!strcasecmp(ptr, "CTRL_@")) {
						keys[definedkeys][index++] = 0;
					} else if(!strcasecmp(ptr, "CTRL_A")) {
						keys[definedkeys][index++] = 1;
					} else if(!strcasecmp(ptr, "CTRL_B")) {
						keys[definedkeys][index++] = 2;
					} else if(!strcasecmp(ptr, "CTRL_C")) {
						keys[definedkeys][index++] = 3;
					} else if(!strcasecmp(ptr, "CTRL_D")) {
						keys[definedkeys][index++] = 4;
					} else if(!strcasecmp(ptr, "CTRL_E")) {
						keys[definedkeys][index++] = 5;
					} else if(!strcasecmp(ptr, "CTRL_F")) {
						keys[definedkeys][index++] = 6;
					} else if(!strcasecmp(ptr, "CTRL_G")) {
						keys[definedkeys][index++] = 7;
					} else if(!strcasecmp(ptr, "CTRL_H")) {
						keys[definedkeys][index++] = 8;
					} else if(!strcasecmp(ptr, "TAB")) {
						keys[definedkeys][index++] = 9;
					} else if(!strcasecmp(ptr, "CTRL_I")) {
						keys[definedkeys][index++] = 9;
					} else if(!strcasecmp(ptr, "ENTER")) {
						keys[definedkeys][index++] = 10;
					} else if(!strcasecmp(ptr, "CTRL_J")) {
						keys[definedkeys][index++] = 10;
					} else if(!strcasecmp(ptr, "CTRL_K")) {
						keys[definedkeys][index++] = 11;
					} else if(!strcasecmp(ptr, "CTRL_L")) {
						keys[definedkeys][index++] = 12;
					} else if(!strcasecmp(ptr, "CTRL_M")) {
						keys[definedkeys][index++] = 13;
					} else if(!strcasecmp(ptr, "CTRL_N")) {
						keys[definedkeys][index++] = 14;
					} else if(!strcasecmp(ptr, "CTRL_O")) {
						keys[definedkeys][index++] = 15;
					} else if(!strcasecmp(ptr, "CTRL_P")) {
						keys[definedkeys][index++] = 16;
					} else if(!strcasecmp(ptr, "CTRL_Q")) {
						keys[definedkeys][index++] = 17;
					} else if(!strcasecmp(ptr, "CTRL_R")) {
						keys[definedkeys][index++] = 18;
					} else if(!strcasecmp(ptr, "CTRL_S")) {
						keys[definedkeys][index++] = 19;
					} else if(!strcasecmp(ptr, "CTRL_T")) {
						keys[definedkeys][index++] = 20;
					} else if(!strcasecmp(ptr, "CTRL_U")) {
						keys[definedkeys][index++] = 21;
					} else if(!strcasecmp(ptr, "CTRL_V")) {
						keys[definedkeys][index++] = 22;
					} else if(!strcasecmp(ptr, "CTRL_W")) {
						keys[definedkeys][index++] = 23;
					} else if(!strcasecmp(ptr, "CTRL_X")) {
						keys[definedkeys][index++] = 24;
					} else if(!strcasecmp(ptr, "CTRL_Y")) {
						keys[definedkeys][index++] = 25;
					} else if(!strcasecmp(ptr, "CTRL_Z")) {
						keys[definedkeys][index++] = 26;
					} else if(!strcasecmp(ptr, "CTRL_[")) {
						keys[definedkeys][index++] = 27;
					} else if(!strcasecmp(ptr, "CTRL_\\")) {
						keys[definedkeys][index++] = 28;
					} else if(!strcasecmp(ptr, "CTRL_]")) {
						keys[definedkeys][index++] = 29;
					} else if(!strcasecmp(ptr, "CTRL_^")) {
						keys[definedkeys][index++] = 30;

					/* Curses keys */
					} else if(!strcasecmp(ptr, "KEY_BREAK")) {
						keys[definedkeys][index++] = KEY_BREAK;
					} else if(!strcasecmp(ptr, "KEY_UP")) {
						keys[definedkeys][index++] = KEY_UP;
					} else if(!strcasecmp(ptr, "KEY_DOWN")) {
						keys[definedkeys][index++] = KEY_DOWN;
					} else if(!strcasecmp(ptr, "KEY_RIGHT")) {
						keys[definedkeys][index++] = KEY_RIGHT;
					} else if(!strcasecmp(ptr, "KEY_LEFT")) {
						keys[definedkeys][index++] = KEY_LEFT;
					} else if(!strcasecmp(ptr, "KEY_HOME")) {
						keys[definedkeys][index++] = KEY_HOME;
					} else if(!strcasecmp(ptr, "KEY_BACKSPACE")) {
						keys[definedkeys][index++] = KEY_BACKSPACE;
					} else if(!strcasecmp(ptr, "KEY_F10")) {
						keys[definedkeys][index++] = KEY_F(10);
					} else if(!strcasecmp(ptr, "KEY_F11")) {
						keys[definedkeys][index++] = KEY_F(11);
					} else if(!strcasecmp(ptr, "KEY_F12")) {
						keys[definedkeys][index++] = KEY_F(12);
					} else if(!strcasecmp(ptr, "KEY_F0")) {
						keys[definedkeys][index++] = KEY_F(0);
					} else if(!strcasecmp(ptr, "KEY_F1")) {
						keys[definedkeys][index++] = KEY_F(1);
					} else if(!strcasecmp(ptr, "KEY_F2")) {
						keys[definedkeys][index++] = KEY_F(2);
					} else if(!strcasecmp(ptr, "KEY_F3")) {
						keys[definedkeys][index++] = KEY_F(3);
					} else if(!strcasecmp(ptr, "KEY_F4")) {
						keys[definedkeys][index++] = KEY_F(4);
					} else if(!strcasecmp(ptr, "KEY_F5")) {
						keys[definedkeys][index++] = KEY_F(5);
					} else if(!strcasecmp(ptr, "KEY_F6")) {
						keys[definedkeys][index++] = KEY_F(6);
					} else if(!strcasecmp(ptr, "KEY_F7")) {
						keys[definedkeys][index++] = KEY_F(7);
					} else if(!strcasecmp(ptr, "KEY_F8")) {
						keys[definedkeys][index++] = KEY_F(8);
					} else if(!strcasecmp(ptr, "KEY_F9")) {
						keys[definedkeys][index++] = KEY_F(9);
					} else if(!strcasecmp(ptr, "KEY_DL")) {
						keys[definedkeys][index++] = KEY_DL;
					} else if(!strcasecmp(ptr, "KEY_IL")) {
						keys[definedkeys][index++] = KEY_IL;
					} else if(!strcasecmp(ptr, "KEY_DC")) {
						keys[definedkeys][index++] = KEY_DC;
					} else if(!strcasecmp(ptr, "KEY_IC")) {
						keys[definedkeys][index++] = KEY_IC;
					} else if(!strcasecmp(ptr, "KEY_EIC")) {
						keys[definedkeys][index++] = KEY_EIC;
					} else if(!strcasecmp(ptr, "KEY_CLEAR")) {
						keys[definedkeys][index++] = KEY_CLEAR;
					} else if(!strcasecmp(ptr, "KEY_EOS")) {
						keys[definedkeys][index++] = KEY_EOS;
					} else if(!strcasecmp(ptr, "KEY_EOL")) {
						keys[definedkeys][index++] = KEY_EOL;
					} else if(!strcasecmp(ptr, "KEY_SF")) {
						keys[definedkeys][index++] = KEY_SF;
					} else if(!strcasecmp(ptr, "KEY_SR")) {
						keys[definedkeys][index++] = KEY_SR;
					} else if(!strcasecmp(ptr, "KEY_NPAGE")) {
						keys[definedkeys][index++] = KEY_NPAGE;
					} else if(!strcasecmp(ptr, "KEY_PPAGE")) {
						keys[definedkeys][index++] = KEY_PPAGE;
					} else if(!strcasecmp(ptr, "KEY_STAB")) {
						keys[definedkeys][index++] = KEY_STAB;
					} else if(!strcasecmp(ptr, "KEY_CTAB")) {
						keys[definedkeys][index++] = KEY_CTAB;
					} else if(!strcasecmp(ptr, "KEY_CATAB")) {
						keys[definedkeys][index++] = KEY_CATAB;
					} else if(!strcasecmp(ptr, "KEY_ENTER")) {
						keys[definedkeys][index++] = KEY_ENTER;
					} else if(!strcasecmp(ptr, "KEY_SRESET")) {
						keys[definedkeys][index++] = KEY_SRESET;
					} else if(!strcasecmp(ptr, "KEY_RESET")) {
						keys[definedkeys][index++] = KEY_RESET;
					} else if(!strcasecmp(ptr, "KEY_PRINT")) {
						keys[definedkeys][index++] = KEY_PRINT;
					} else if(!strcasecmp(ptr, "KEY_LL")) {
						keys[definedkeys][index++] = KEY_LL;
					} else if(!strcasecmp(ptr, "KEY_A1")) {
						keys[definedkeys][index++] = KEY_A1;
					} else if(!strcasecmp(ptr, "KEY_A3")) {
						keys[definedkeys][index++] = KEY_A3;
					} else if(!strcasecmp(ptr, "KEY_B2")) {
						keys[definedkeys][index++] = KEY_B2;
					} else if(!strcasecmp(ptr, "KEY_C1")) {
						keys[definedkeys][index++] = KEY_C1;
					} else if(!strcasecmp(ptr, "KEY_C3")) {
						keys[definedkeys][index++] = KEY_C3;
					} else if(!strcasecmp(ptr, "KEY_BTAB")) {
						keys[definedkeys][index++] = KEY_BTAB;
					} else if(!strcasecmp(ptr, "KEY_BEG")) {
						keys[definedkeys][index++] = KEY_BEG;
					} else if(!strcasecmp(ptr, "KEY_CANCEL")) {
						keys[definedkeys][index++] = KEY_CANCEL;
					} else if(!strcasecmp(ptr, "KEY_CLOSE")) {
						keys[definedkeys][index++] = KEY_CLOSE;
					} else if(!strcasecmp(ptr, "KEY_COMMAND")) {
						keys[definedkeys][index++] = KEY_COMMAND;
					} else if(!strcasecmp(ptr, "KEY_COPY")) {
						keys[definedkeys][index++] = KEY_COPY;
					} else if(!strcasecmp(ptr, "KEY_CREATE")) {
						keys[definedkeys][index++] = KEY_CREATE;
					} else if(!strcasecmp(ptr, "KEY_END")) {
						keys[definedkeys][index++] = KEY_END;
					} else if(!strcasecmp(ptr, "KEY_EXIT")) {
						keys[definedkeys][index++] = KEY_EXIT;
					} else if(!strcasecmp(ptr, "KEY_FIND")) {
						keys[definedkeys][index++] = KEY_FIND;
					} else if(!strcasecmp(ptr, "KEY_HELP")) {
						keys[definedkeys][index++] = KEY_HELP;
					} else if(!strcasecmp(ptr, "KEY_MARK")) {
						keys[definedkeys][index++] = KEY_MARK;
					} else if(!strcasecmp(ptr, "KEY_MESSAGE")) {
						keys[definedkeys][index++] = KEY_MESSAGE;
					} else if(!strcasecmp(ptr, "KEY_MOUSE")) {
						keys[definedkeys][index++] = KEY_MOUSE;
					} else if(!strcasecmp(ptr, "KEY_NEXT")) {
						keys[definedkeys][index++] = KEY_NEXT;
					} else if(!strcasecmp(ptr, "KEY_OPEN")) {
						keys[definedkeys][index++] = KEY_OPEN;
					} else if(!strcasecmp(ptr, "KEY_OPTIONS")) {
						keys[definedkeys][index++] = KEY_OPTIONS;
					} else if(!strcasecmp(ptr, "KEY_PREVIOUS")) {
						keys[definedkeys][index++] = KEY_PREVIOUS;
					} else if(!strcasecmp(ptr, "KEY_REDO")) {
						keys[definedkeys][index++] = KEY_REDO;
					} else if(!strcasecmp(ptr, "KEY_REFERENCE")) {
						keys[definedkeys][index++] = KEY_REFERENCE;
					} else if(!strcasecmp(ptr, "KEY_REFRESH")) {
						keys[definedkeys][index++] = KEY_REFRESH;
					} else if(!strcasecmp(ptr, "KEY_REPLACE")) {
						keys[definedkeys][index++] = KEY_REPLACE;
					} else if(!strcasecmp(ptr, "KEY_RESTART")) {
						keys[definedkeys][index++] = KEY_RESTART;
					} else if(!strcasecmp(ptr, "KEY_RESUME")) {
						keys[definedkeys][index++] = KEY_RESUME;
					} else if(!strcasecmp(ptr, "KEY_SAVE")) {
						keys[definedkeys][index++] = KEY_SAVE;
					} else if(!strcasecmp(ptr, "KEY_SBEG")) {
						keys[definedkeys][index++] = KEY_SBEG;
					} else if(!strcasecmp(ptr, "KEY_SCANCEL")) {
						keys[definedkeys][index++] = KEY_SCANCEL;
					} else if(!strcasecmp(ptr, "KEY_SCOMMAND")) {
						keys[definedkeys][index++] = KEY_SCOMMAND;
					} else if(!strcasecmp(ptr, "KEY_SCOPY")) {
						keys[definedkeys][index++] = KEY_SCOPY;
					} else if(!strcasecmp(ptr, "KEY_SCREATE")) {
						keys[definedkeys][index++] = KEY_SCREATE;
					} else if(!strcasecmp(ptr, "KEY_SDC")) {
						keys[definedkeys][index++] = KEY_SDC;
					} else if(!strcasecmp(ptr, "KEY_SDL")) {
						keys[definedkeys][index++] = KEY_SDL;
					} else if(!strcasecmp(ptr, "KEY_SELECT")) {
						keys[definedkeys][index++] = KEY_SELECT;
					} else if(!strcasecmp(ptr, "KEY_SEND")) {
						keys[definedkeys][index++] = KEY_SEND;
					} else if(!strcasecmp(ptr, "KEY_SEOL")) {
						keys[definedkeys][index++] = KEY_SEOL;
					} else if(!strcasecmp(ptr, "KEY_SEXIT")) {
						keys[definedkeys][index++] = KEY_SEXIT;
					} else if(!strcasecmp(ptr, "KEY_SFIND")) {
						keys[definedkeys][index++] = KEY_SFIND;
					} else if(!strcasecmp(ptr, "KEY_SHELP")) {
						keys[definedkeys][index++] = KEY_SHELP;
					} else if(!strcasecmp(ptr, "KEY_SHOME")) {
						keys[definedkeys][index++] = KEY_SHOME;
					} else if(!strcasecmp(ptr, "KEY_SIC")) {
						keys[definedkeys][index++] = KEY_SIC;
					} else if(!strcasecmp(ptr, "KEY_SLEFT")) {
						keys[definedkeys][index++] = KEY_SLEFT;
					} else if(!strcasecmp(ptr, "KEY_SMESSAGE")) {
						keys[definedkeys][index++] = KEY_SMESSAGE;
					} else if(!strcasecmp(ptr, "KEY_SMOVE")) {
						keys[definedkeys][index++] = KEY_SMOVE;
					} else if(!strcasecmp(ptr, "KEY_SNEXT")) {
						keys[definedkeys][index++] = KEY_SNEXT;
					} else if(!strcasecmp(ptr, "KEY_OPTIONS")) {
						keys[definedkeys][index++] = KEY_OPTIONS;
					} else if(!strcasecmp(ptr, "KEY_PREVIOUS")) {
						keys[definedkeys][index++] = KEY_PREVIOUS;
					} else if(!strcasecmp(ptr, "KEY_SPRINT")) {
						keys[definedkeys][index++] = KEY_SPRINT;
					} else if(!strcasecmp(ptr, "KEY_SREDO")) {
						keys[definedkeys][index++] = KEY_SREDO;
					} else if(!strcasecmp(ptr, "KEY_SREPLACE")) {
						keys[definedkeys][index++] = KEY_SREPLACE;
					} else if(!strcasecmp(ptr, "KEY_SRIGHT")) {
						keys[definedkeys][index++] = KEY_SRIGHT;
					} else if(!strcasecmp(ptr, "KEY_SRSUME")) {
						keys[definedkeys][index++] = KEY_SRSUME;
					} else if(!strcasecmp(ptr, "KEY_SSAVE")) {
						keys[definedkeys][index++] = KEY_SSAVE;
					} else if(!strcasecmp(ptr, "KEY_SSUSPEND")) {
						keys[definedkeys][index++] = KEY_SSUSPEND;
					} else if(!strcasecmp(ptr, "KEY_SUNDO")) {
						keys[definedkeys][index++] = KEY_SUNDO;
					} else if(!strcasecmp(ptr, "KEY_SUSPEND")) {
						keys[definedkeys][index++] = KEY_SUSPEND;
					} else if(!strcasecmp(ptr, "KEY_UNDO")) {
						keys[definedkeys][index++] = KEY_UNDO;
					}
					if(index >= 9) break;
				}
				keys[definedkeys++][index++] = -1;
			}
		} else if(!strncasecmp(ptr, "[misc]", 6)) {
			while(fgets(tmpstr, sizeof(tmpstr), file)) {
				ptr = tmpstr;
				strip_comment(ptr);
				if(*ptr == '[') {
					break;
				} else if(!strncasecmp(ptr, "tab_size=", 9)) {
					tab_size = strtol(ptr + 9, 0, 0);
				} else if(!strncasecmp(ptr, "page_skip=", 10)) {
					page_skip = strtol(ptr + 10, 0, 0);
				} else if(!strncasecmp(ptr, "save_clipboard=", 15)) {
					save_clipboard = strtol(ptr + 15, 0, 0);
				} else if(!strncasecmp(ptr, "lock_files=", 11)) {
					lock_files = strtol(ptr + 11, 0, 0);
				} else if(!strncasecmp(ptr, "color_term=", 11)) {
					color_term = strtol(ptr + 11, 0, 0);
				} else if(!strncasecmp(ptr, "window_margin=", 14)) {
					window_margin = strtol(ptr + 14, 0, 0);
				} else if(!strncasecmp(ptr, "seek_centers=", 13)) {
					seek_centers = strtol(ptr + 13, 0, 0);
				}
			}
		}
	}
	fclose(file);
	return;
}

int configure() {
	char tmpstr[MAXPATHLEN];

	definedkeys = 0;

	tab_size = -1;
	page_skip = -1;
	save_clipboard = -1;
	lock_files = -1;
	window_margin = -1;
	seek_centers = -1;
	configure_file("/etc/puff.conf");
	strlcpy(tmpstr, homedir, sizeof(tmpstr));
	strlcat(tmpstr, "/.puff.conf", sizeof(tmpstr));
	configure_file(tmpstr);
	configure_file("/etc/puff.conf.fixed");

	if(!definedkeys) {
		keys[definedkeys][0] = KUP;         keys[definedkeys][1] = KEY_UP;      keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KDOWN;       keys[definedkeys][1] = KEY_DOWN;    keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KRIGHT;      keys[definedkeys][1] = KEY_RIGHT;   keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KLEFT;       keys[definedkeys][1] = KEY_LEFT;    keys[definedkeys++][2] = -1;

		keys[definedkeys][0] = KUP;         keys[definedkeys][1] = 27;          keys[definedkeys][2] = '[';         keys[definedkeys][3] = 'A';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KDOWN;       keys[definedkeys][1] = 27;          keys[definedkeys][2] = '[';         keys[definedkeys][3] = 'B';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KRIGHT;      keys[definedkeys][1] = 27;          keys[definedkeys][2] = '[';         keys[definedkeys][3] = 'C';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KLEFT;       keys[definedkeys][1] = 27;          keys[definedkeys][2] = '[';         keys[definedkeys][3] = 'D';         keys[definedkeys++][4] = -1;

		keys[definedkeys][0] = KUP;         keys[definedkeys][1] = 27;          keys[definedkeys][2] = 'O';         keys[definedkeys][3] = 'A';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KDOWN;       keys[definedkeys][1] = 27;          keys[definedkeys][2] = 'O';         keys[definedkeys][3] = 'B';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KRIGHT;      keys[definedkeys][1] = 27;          keys[definedkeys][2] = 'O';         keys[definedkeys][3] = 'C';         keys[definedkeys++][4] = -1;
		keys[definedkeys][0] = KLEFT;       keys[definedkeys][1] = 27;          keys[definedkeys][2] = 'O';         keys[definedkeys][3] = 'D';         keys[definedkeys++][4] = -1;

		keys[definedkeys][0] = KBOL;        keys[definedkeys][1] = KEY_HOME;    keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KEOL;        keys[definedkeys][1] = KEY_END;     keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KPPAGE;      keys[definedkeys][1] = KEY_PPAGE;   keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KNPAGE;      keys[definedkeys][1] = KEY_NPAGE;   keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KBACKSPACE;  keys[definedkeys][1] = KEY_BACKSPACE; keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KINSERT;     keys[definedkeys][1] = KEY_IC;      keys[definedkeys++][2] = -1;
		keys[definedkeys][0] = KDELETE;     keys[definedkeys][1] = KEY_DC;      keys[definedkeys++][2] = -1;

		/* ctrl-a */
		keys[definedkeys][0] = KBOL;        keys[definedkeys][1] = 1;           keys[definedkeys++][2] = -1;
		/* ctrl-b */
		keys[definedkeys][0] = KSEEKBYTE;   keys[definedkeys][1] = 2;           keys[definedkeys++][2] = -1;
		/* ctrl-c */
		keys[definedkeys][0] = KCANCEL;     keys[definedkeys][1] = 3;           keys[definedkeys++][2] = -1;
		/* ctrl-d */
		keys[definedkeys][0] = KDELETE;     keys[definedkeys][1] = 4;           keys[definedkeys++][2] = -1;
		/* ctrl-e */
		keys[definedkeys][0] = KEOL;        keys[definedkeys][1] = 5;           keys[definedkeys++][2] = -1;
		/* ctrl-f */
		keys[definedkeys][0] = KFILEMENU;   keys[definedkeys][1] = 6;           keys[definedkeys++][2] = -1;
		/* ctrl-i / tab */
		keys[definedkeys][0] = KTAB;        keys[definedkeys][1] = 9;           keys[definedkeys++][2] = -1;
		/* ctrl-j / ctrl-m / '\n' */
		keys[definedkeys][0] = KENTER;      keys[definedkeys][1] = 10;          keys[definedkeys++][2] = -1;
		/* ctrl-k */
		keys[definedkeys][0] = KCUT;        keys[definedkeys][1] = 11;          keys[definedkeys++][2] = -1;
		/* ctrl-l */
		keys[definedkeys][0] = KREDRAW;     keys[definedkeys][1] = 12;          keys[definedkeys++][2] = -1;
		/* ctrl-n */
		keys[definedkeys][0] = KCLEARCLIP;  keys[definedkeys][1] = 14;          keys[definedkeys++][2] = -1;
		/* ctrl-p */
		keys[definedkeys][0] = KPASTE;      keys[definedkeys][1] = 16;          keys[definedkeys++][2] = -1;
		/* ctrl-s */
		keys[definedkeys][0] = KQSAVE;      keys[definedkeys][1] = 19;          keys[definedkeys++][2] = -1;
		/* ctrl-t */
		keys[definedkeys][0] = KSEEKLINE;   keys[definedkeys][1] = 20;          keys[definedkeys++][2] = -1;
		/* ctrl-u */
		keys[definedkeys][0] = KKILL;       keys[definedkeys][1] = 21;          keys[definedkeys++][2] = -1;
		/* ctrl-v */
		keys[definedkeys][0] = KNPAGE;      keys[definedkeys][1] = 22;          keys[definedkeys++][2] = -1;
		/* ctrl-w */
		keys[definedkeys][0] = KSEARCH;     keys[definedkeys][1] = 23;          keys[definedkeys++][2] = -1;
		/* ctrl-x */
		keys[definedkeys][0] = KEXIT;       keys[definedkeys][1] = 24;          keys[definedkeys++][2] = -1;
		/* ctrl-y */
		keys[definedkeys][0] = KPPAGE;      keys[definedkeys][1] = 25;          keys[definedkeys++][2] = -1;
		/* ctrl-z */
		keys[definedkeys][0] = KSUSPEND;    keys[definedkeys][1] = 26;          keys[definedkeys++][2] = -1;
		/* ctrl-\ */
		keys[definedkeys][0] = KSPECIAL;    keys[definedkeys][1] = 28;          keys[definedkeys++][2] = -1;
	}

	if(tab_size < 1 || tab_size > 20) tab_size = 8;
	if(page_skip < 0 || page_skip > LINES) page_skip = 1;
	if(window_margin < 0 || window_margin > LINES / 2) window_margin = 0;

	return OK;
}

void sigs(int sig) {
	volatile static int raisedsignal = 0, nextsignal = 0;

	/* Don't let the signal handler run within itself */
	/* We only enqueue one signal because repeated SIGWINCHs don't matter
	 * as long as the last one is handled.  SIGHUP causes a re-configure
	 * and the config file won't change that quick, and the others cause us
	 * to exit anyway. */
	if(raisedsignal) {
		nextsignal = sig;
		return;
	}

next_signal:
	raisedsignal = sig;

	if(sig == SIGHUP) {
		configure();
		if(curfile != -1) display_file(curfile);
	} else if(sig == SIGINT || sig == SIGTERM || sig == SIGXCPU || sig == SIGXFSZ) {
		int tmpint, tmpint2;
		char filename[MAXPATHLEN];
		struct stat sb;

		tmpint2 = -1;
		for(tmpint = 1; tmpint < PUFF_MAX_FILES; tmpint++) {
			if(finfo[tmpint].fp && finfo[tmpint].modified) {
				if(finfo[tmpint].name[0]) {
					if(snprintf(filename, sizeof(filename), "%s.changes", finfo[tmpint].name) >= sizeof(filename)) continue;
					file_backup(finfo[tmpint].fp, filename);
					continue;
				} else {
					while(++tmpint2 < 1000) {
						if(snprintf(filename, sizeof(filename), "%s/.puff.bak.%i", finfo[tmpint].name, tmpint2) >= sizeof(filename)) break;
						if(stat(filename, &sb)) {
							/* Don't overwrite existing files */
							if(errno == ENOENT) {
								file_saveas(finfo[tmpint].fp, filename, 0, S_IRUSR|S_IWUSR);
							}
							break;
						}
					}
				}
			}
		}
		if(save_clipboard && snprintf(filename, sizeof(filename), "%s/.puff.clipboard", homedir) < sizeof(filename)) {
			file_saveas(finfo[0].fp, filename, (lock_files?FILE_LOCK:0)|FILE_ALLOWOVERWRITE, S_IRUSR|S_IWUSR);
		}

		endwin();
		if(sig == SIGINT) {
			printf("Interrupted.\n");
		} else if(sig == SIGTERM) {
			printf("Terminated.\n");
		} else if(sig == SIGXCPU) {
			printf("CPU time limit exceeded.\n");
		} else if(sig == SIGXFSZ) {
			printf("File size limit exceeded.\n");
		}
		exit(1);
#ifndef __sun__   /* SunOS appears to be missing resizeterm() and wresize(). */
	} else if(sig == SIGWINCH) {
		struct winsize ws;
		int tmpint;

		/* Fix term size */
		if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != -1) {
			if(ws.ws_col < 5 || ws.ws_row < 2) {
				printf("Term too small!");

				/* Do -not- exit the signal handler without resetting
				 * raisedsignal. */
				goto exit_sig;
			}
			if(ws.ws_col > MAX_COLS) ws.ws_col = MAX_COLS;
			resizeterm(ws.ws_row, ws.ws_col);
		}

		/* Fix status win */
		mvwin(statuswin, LINES - 1, 0);
		wresize(statuswin, 1, COLS);

		/* Move file wins */
		for(tmpint = 0; tmpint < PUFF_MAX_FILES; tmpint++) {
			if(finfo[tmpint].fp) {
				mvwin(finfo[tmpint].win, 0, 0);
				wresize(finfo[tmpint].win, LINES-1, COLS);
				finfo[tmpint].hsize = COLS;
				finfo[tmpint].vsize = LINES-1;
				display_file(tmpint);
				display_line(tmpint, -1);
			}
		}

		if(page_skip > LINES) page_skip = 1;
		redraw_statuswin(0);
		if(curfile != -1) {
			redrawwin(WIN);
			wnoutrefresh(WIN);
		}
		if(messagewin) {
			wresize(messagewin, LINES - 1, COLS);
			redrawwin(messagewin);
			wnoutrefresh(messagewin);
		}
		doupdate();
		wrefresh(curscr);
#endif

#ifdef DEBUG
	} else if(sig == SIGUSR1) {
		file_debug_dump(FP);
		printf("lock_files = %i\r\n", lock_files);
		printf("color_term = %i\r\n", color_term);
		printf("topline = %i\tvcursor = %i\thcursor = %i\trealh = %i\toffset = %i\r\n",
				TOPLINE, VCURSOR, (int)HCURSOR, (int)REALH, (int)OFFSET);
		printf("memusage:  %i\r\n", file_memusage(FP));
		fflush(stdout);
#endif
	}

exit_sig:
	if(nextsignal) {
		sig = nextsignal;
		nextsignal = 0;
		goto next_signal;
	}
	raisedsignal = 0;
}

int seekbyte(int curfile, off_t offset) {
	int linenum; /* 0-based index */
	int oldtopline; /* To optimize text output by scrolling if possible */
	int loop;

	/* Don't seek to a negative offset. */
	if(offset < 0) return ERR;
	if(offset > file_size(FP))
		offset = file_size(FP);
	linenum = file_findoff(FP, offset);
	if(linenum == -1) return ERR;

	oldtopline = TOPLINE;

	/* If the cursor is out of bounds, fix it. */
	if(VCURSOR < window_margin)
		VCURSOR = window_margin;
	else if(VCURSOR > VSIZE - window_margin - 1)
		VCURSOR = VSIZE - window_margin - 1;

	if(seek_centers) {
		/* This flag means to center the cursor vertically, if possible */
		VCURSOR = VSIZE / 2;
		TOPLINE = linenum - VCURSOR;
	} else {
		if(linenum < TOPLINE + window_margin || linenum >= TOPLINE + VSIZE - window_margin - 1) {
			/* It's off the screen, don't move the cursor. */
			TOPLINE = linenum - VCURSOR;
		} else {
			/* Just move the cursor. */
			VCURSOR = linenum - TOPLINE;
	}	}

	if(TOPLINE < 0) {
		/* This might require the cursor to move above window_margin. */
		TOPLINE = 0;
		VCURSOR = linenum;
	}
	/* Now we're on the right line. */

	/* Update display */
	if(abs(TOPLINE - oldtopline) < VSIZE) {
		/* Scroll */
		scrollok(finfo[curfile].win, TRUE);
		wscrl(finfo[curfile].win, TOPLINE - oldtopline);
		scrollok(finfo[curfile].win, FALSE);

		/* Redraw lines that appeared because of the scroll. */
		if(TOPLINE < oldtopline) {
			/* Scrolled up - redraw lines at the top of the screen. */
			loop = oldtopline - TOPLINE;
			while(loop--)
				display_line(curfile, loop);
		} else if(TOPLINE > oldtopline) {
			/* Scrolled down - redraw lines at the bottom of the screen. */
			loop = TOPLINE - oldtopline;
			while(loop--)
				display_line(curfile, VSIZE - loop - 1);
		}
	} else {
		/* Complete redraw if all the lines from before scrolled off. */
		display_file(curfile);
	}

	/* Position cursor horizontally. */
	HCURSOR = REALH = 0;
	display_line(curfile, -1);	/* get accurate offset */
	REALH = offset - OFFSET;	/* approximate horiz position */
	display_line(curfile, -1);
	while(abs(OFFSET - offset) > tab_size * 3) {
		/* speed optimization - get close quickly, not really necessary, but
		 * EOL can be -really- slow without this */
		HCURSOR += offset - OFFSET;
		display_line(curfile, -1);
		REALH = HCURSOR;
	}
	while(OFFSET != offset) {
		if(OFFSET < offset) {	/* exact horiz position */
			if(keyright() == ERR) break;
		} else {
			if(keyleft() == ERR) break;
		}
		display_line(curfile, -1);
	}

	return OK;
}

int keynothing() {
	return OK;
}

int keybol() {
	if(curfile == -1) return ERR;
	HCURSOR = 0;
	return OK;
}

int keyeol() {
	off_t offset;

	if(curfile == -1) return ERR;
	offset = file_findline(FP, TOPLINE + VCURSOR + 1);
	if(offset == -1) offset = file_size(FP);
	else if(offset != 0) offset--;

	seekbyte(curfile, offset);
	return OK;
}

int keyup() {
	if(curfile == -1) return ERR;
	display_line(curfile, VCURSOR);		/* move current line back to beginning of line */

	if(VCURSOR > window_margin || TOPLINE == 0) {
		if(VCURSOR == 0) return ERR;	/* don't scroll past the top of the document */
		/* no need to scroll */
		VCURSOR--;
		return OK;
	}

	/* Scroll Up */
	scrollok(finfo[curfile].win, TRUE);
	wscrl(finfo[curfile].win, -1);
	scrollok(finfo[curfile].win, FALSE);
	TOPLINE--;

	display_line(curfile, 0);	/* Redraw the line that just appeared */
	return OK;
}

int keydown() {
	if(curfile == -1) return ERR;
	display_line(curfile, VCURSOR);		/* move current line back to beginning of line */

	if(file_findline(FP, TOPLINE + VCURSOR + 1) == -1)
		return ERR;	/* can't scroll down - EOF */

	if(VCURSOR < VSIZE - window_margin - 1) {
		/* no need to scroll */
		VCURSOR++;
		return OK;
	}

	/* Scroll Down */
	scrollok(finfo[curfile].win, TRUE);
	wscrl(finfo[curfile].win, 1);
	scrollok(finfo[curfile].win, FALSE);
	TOPLINE++;

	display_line(curfile, VSIZE - 1);
	return OK;
}

int keyleft() {
	int tmpint;

	/* HCURSOR stores where the cursor wants to be, and REALH stores
	 * where the cursor actually is.  REALH is displayed on the status
	 * line.  display_line gets where to put the cursor from HCURSOR and
	 * updates REALH. */
	if(curfile == -1) return ERR;
	HCURSOR = REALH;
	if(HCURSOR == 0) {
		if(keyup() == ERR || keyeol() == ERR) return ERR;
		return OK;
	}
	tmpint = file_getc(FP, OFFSET - 1);
	if(tmpint == -1)
		return ERR;
	if(tmpint == '\t') {
		HCURSOR--;
		display_line(curfile, -1);	/* calculate REALH */
		HCURSOR = REALH;
		return OK;
	}
	if(tmpint < 32 || tmpint == 127) {
		/* ^X style character */
		HCURSOR -= 2;
		return OK;
	}
	HCURSOR--;
	return OK;
}

int keyright() {
	int tmpint;

	if(curfile == -1) return ERR;
	HCURSOR = REALH;
	tmpint = file_getc(FP, OFFSET);
	if(tmpint == -1)
		return ERR;
	if(tmpint == '\n') {
		if(keydown() == ERR || keybol() == ERR) return ERR;
		return OK;
	}
	if(tmpint == '\t') {
		HCURSOR += tab_size - HCURSOR % tab_size;
		return OK;
	}
	if(tmpint < 32 || tmpint == 127) {
		/* ^X style character */
		HCURSOR += 2;
		return OK;
	}
	HCURSOR++;
	return OK;
}

int keykillbol() {
	off_t tmpoff, tmpoff2;
	int tmpint;

	if(curfile == -1) return ERR;
	tmpoff = file_findline(FP, TOPLINE + VCURSOR); /* beginning of cut */
	tmpoff2 = OFFSET; /* end of cut */
	if(tmpoff2 < 0) tmpoff2 = file_size(FP);
	tmpint = tmpoff2 - tmpoff;	/* translation: size = end - begin */
	file_delete(FP, tmpoff, tmpint);
	display_file(curfile);
	HCURSOR = REALH = 0;
	MODIFIED = 1;
	return OK;
}

int keykilleol() {
	off_t tmpoff, tmpoff2;
	int tmpint;

	if(curfile == -1) return ERR;
	tmpoff = OFFSET; /* beginning of cut */
	tmpoff2 = file_findline(FP, TOPLINE + VCURSOR + 1) - 1; /* end of cut */
	if(tmpoff2 < 0) tmpoff2 = file_size(FP);
	tmpint = tmpoff2 - tmpoff;	/* translation: size = end - begin */
	file_delete(FP, tmpoff, tmpint);
	display_file(curfile);
	MODIFIED = 1;
	return OK;
}

int keyppage() {
	if(curfile == -1) return ERR;
	if(TOPLINE == 0) {
		/* Already on the top page, so go to the top of it. */
		display_line(curfile, VCURSOR);
		VCURSOR = 0;
		HCURSOR = 0;
		return OK;
	}
	TOPLINE -= VSIZE - page_skip;
	if(TOPLINE < 0) TOPLINE = 0;
	display_file(curfile);
	return OK;
}

int keynpage() {
	if(curfile == -1) return ERR;
	if(file_findline(FP, TOPLINE + VSIZE - page_skip) == -1) {
		/* Already on the last page, so go to the bottom of it. */
		display_line(curfile, VCURSOR);
		VCURSOR = file_numlines(FP) - TOPLINE - 1;
		keyeol();
		return OK;
	}
	TOPLINE += VSIZE - page_skip;
	if(file_findline(FP, TOPLINE + VCURSOR) == -1) {
		/* VCURSOR is below the end of the file. */
		VCURSOR = file_numlines(FP) - TOPLINE - 1;
		keyeol();
	}
	display_file(curfile);
	return OK;
}

int keybackspace() {
	int tmpint;
	off_t tmpoff;

	if(curfile == -1) return ERR;
	if(OFFSET == 0) return ERR;
	HCURSOR = REALH;
	tmpoff = OFFSET - 1;
	if(HCURSOR) tmpint = 0; else tmpint = 1;
	if(keyleft() == ERR) return ERR;
	if(file_delete(FP, tmpoff, 1) != 1)
		return ERR;
	finfo[curfile].modified = 1;
	if(tmpint) display_file(curfile);
	return OK;
}

int keydelete() {
	int tmpint;

	if(curfile == -1) return ERR;
	HCURSOR = REALH;
	if(file_getc(FP, OFFSET) == '\n') tmpint = 1; else tmpint = 0;
	if(file_delete(FP, OFFSET, 1) != 1)
		return ERR;
	finfo[curfile].modified = 1;
	if(tmpint) display_file(curfile);
	return OK;
}

int keyenter() {
	if(curfile == -1) return ERR;
	HCURSOR = REALH;
	if(file_insert(FP, OFFSET, 1, "\n") != 1) {
		redraw_statuswin("Internal error:  %s", strerror(errno));
		return ERR;
	}
	HCURSOR = 0;
	keydown();
	display_file(curfile);
	finfo[curfile].modified = 1;
	return OK;
}

int keyspecial() {
	char tmpstr[MAX_COLS+1];
	int tmpint;

	if(curfile == -1) return ERR;
	*tmpstr = 0;
	redraw_statuswin("Enter ASCII code:  ");
	if(kgetstr(statuswin, tmpstr, 5, "number") == ERR || !*tmpstr) {
		redraw_statuswin("file");
		return ERR;
	}
	tmpint = strtol(tmpstr, 0, 0);
	HCURSOR = REALH;
	if(file_insert(FP, OFFSET, 1, &tmpint) != 1) {
		redraw_statuswin("Internal error:  %s", strerror(errno));
		return ERR;
	}
	keyright();
	finfo[curfile].modified = 1;
	redraw_statuswin("file");
	return OK;
}

int keyseekline() {
	int tmpint;
	char tmpstr[MAX_COLS+1];
	off_t tmpoff;

	if(curfile == -1) return ERR;
	redraw_statuswin("Enter line:  ");
	tmpstr[0] = 0;
	if(kgetstr(statuswin, tmpstr, 10, "number") == ERR || !tmpstr[0]) {
		redraw_statuswin("file");
		return ERR;
	}
	tmpint = strtol(tmpstr, NULL, 0) - 1;
	if(tmpint < 0) tmpint = 0;
	tmpoff = file_findline(FP, tmpint);
	if(tmpoff == -1) tmpoff = file_findline(FP, file_numlines(FP) - 1);
	if(tmpoff == -1) {
		redraw_statuswin("Error finding offset - Shouldn't happen.");
		return ERR;
	}
	seekbyte(curfile, tmpoff);
	redraw_statuswin("file");
	return OK;
}

int keyseekbyte() {
	char tmpstr[MAX_COLS+1];
	off_t tmpoff;

	if(curfile == -1) return ERR;
	redraw_statuswin("Enter offset:  ");
	tmpstr[0] = 0;
	if(kgetstr(statuswin, tmpstr, 19, "number") == ERR || !*tmpstr) {
		redraw_statuswin("file");
		return ERR;
	}
	tmpoff = strtol(tmpstr, NULL, 0);
	seekbyte(curfile, tmpoff);
	redraw_statuswin("file");
	return OK;
}

int keysearch() {
	static char lastsearch[MAX_COLS+1] = "";
	char tmpstr[MAX_COLS+1];
	int tmpint;
	off_t tmpoff;

	if(curfile == -1) return ERR;
	redraw_statuswin("Search for:  ");
	strcpy(tmpstr, lastsearch);
	if(kgetstr(statuswin, tmpstr, sizeof(tmpstr), "search") == ERR) {
		redraw_statuswin("file");
		return ERR;
	}
	if(!*tmpstr) strcpy(tmpstr, lastsearch);
	else strcpy(lastsearch, tmpstr);
	redraw_statuswin("Searching...");
	tmpint = strlen(tmpstr);
	tmpoff = file_search(FP, tmpstr, tmpint, OFFSET+1, file_size(FP), 0);
	redraw_statuswin("file");
	if(tmpoff == -1) {
		redraw_statuswin("Search wrapped...");
		tmpoff = file_search(FP, tmpstr, tmpint, 0, OFFSET + tmpint, 0);
		if(tmpoff != -1) redraw_statuswin("Search wrapped");
	}
	if(tmpoff == -1) {
		redraw_statuswin("Not found");
		return ERR;
	}
	seekbyte(curfile, tmpoff);
	return OK;
}

int keynew() {
	int tmpint;
	for(tmpint = 1; tmpint < PUFF_MAX_FILES; tmpint++)
		if(!finfo[tmpint].fp) break;
	if(tmpint == PUFF_MAX_FILES) {
		redraw_statuswin("Too many files open.");
		return ERR;
	}
	if(newfile(tmpint, 0) == ERR) redraw_statuswin("Error making new file");
	curfile = tmpint;
	redraw_statuswin("file");
	return OK;
}

int keyopen() {
	int tmpint;
	char tmpstr[MAX_COLS+1];

	for(tmpint = 1; tmpint < PUFF_MAX_FILES; tmpint++)
		if(!finfo[tmpint].fp) break;
	if(tmpint == PUFF_MAX_FILES) {
		redraw_statuswin("Too many files open.");
		return ERR;
	}
	redraw_statuswin("File to open:  ");
	*tmpstr = 0;
	if(kgetstr(statuswin, tmpstr, MAXPATHLEN, "file") == ERR || !*tmpstr) {
		redraw_statuswin("file");
		return ERR;
	}
	if(openfile(tmpint, tmpstr) == ERR) return ERR;
	curfile = tmpint;
	display_file(curfile);
	redraw_statuswin("file");
	return OK;
}

int keyredraw() {
	redraw_statuswin("file");
	sigs(SIGWINCH);
	return OK;
}

int keysuspend() {
	endwin();
	raise(SIGSTOP);
	keyredraw();
	return OK;
}

int keysave() {
	char tmpstr[MAX_COLS+1], tmpchar;
	struct stat sb;

	if(curfile == -1) return ERR;
	redraw_statuswin("Filename:  ");
	if(curfile) strlcpy(tmpstr, NAME, sizeof(tmpstr));
	else *tmpstr = 0;	/* No default name for clipboard */
	if(kgetstr(statuswin, tmpstr, sizeof(tmpstr), "file") == ERR || !*tmpstr) {
		redraw_statuswin("file");
		return ERR;
	}
	if(strcmp(NAME, tmpstr)) {
		if(!stat(tmpstr, &sb)) {
			redraw_statuswin("Overwrite?  ");
			tmpchar = 0;
			if(kgetstr(statuswin, &tmpchar, 1, "ynYN") == ERR || !tmpchar ||
							tmpchar == 'n' || tmpchar == 'N') {
				redraw_statuswin("file");
				return ERR;
			}
		}
	}
	redraw_statuswin("Saving...");
	if(!curfile) {
		/* Don't change the name of the clipboard */
		if(file_saveas(FP, tmpstr, (lock_files?FILE_LOCK:0)|FILE_ALLOWOVERWRITE|FILE_KEEPOLD, S_IRUSR|S_IWUSR)) {
			if(errno == EAGAIN) {
				redraw_statuswin("%s", strerror(errno));
				return ERR;
			}
			/* Only happens if the filesystem's locking is broken */
			if(file_saveas(FP, tmpstr, FILE_ALLOWOVERWRITE|FILE_KEEPOLD, S_IRUSR|S_IWUSR)) {
				redraw_statuswin("%s", strerror(errno));
				return ERR;
			}
			redraw_statuswin("Warning - Could not lock file: %s", strerror(errno));
			sleep(1);
		}
	} else {
		if(file_saveas(FP, tmpstr, (lock_files?FILE_LOCK:0)|FILE_ALLOWOVERWRITE, FILE_MODE666)) {
			if(errno == EAGAIN) {
				redraw_statuswin("%s", strerror(errno));
				return ERR;
			}
			/* Only happens if the filesystem's locking is broken */
			if(file_saveas(FP, tmpstr, FILE_ALLOWOVERWRITE, S_IRUSR|S_IWUSR)) {
				redraw_statuswin("%s", strerror(errno));
				return ERR;
			}
			redraw_statuswin("Warning - Could not lock file: %s", strerror(errno));
			sleep(1);
		}
		strlcpy(NAME, tmpstr, sizeof(NAME));
		MODIFIED = 0;
	}

	if(snprintf(tmpstr, sizeof(tmpstr), "%s.changes", NAME) >= sizeof(tmpstr)) return OK;
	if(stat(tmpstr, &sb)) {
		redraw_statuswin("file");
		return OK;
	}
	redraw_statuswin("Remove %s (you prob'ly should)?  ", tmpstr);
	tmpchar = 0;
	if(kgetstr(statuswin, &tmpchar, 1, "YyNn") == OK && (tmpchar == 'Y' || tmpchar == 'y')) {
		unlink(tmpstr);
	}

	redraw_statuswin("file");
	return OK;
}

int keyqsave() {
	struct stat sb;
	char tmpstr[MAX_COLS+1], tmpchar;

	if(curfile == -1) return ERR;
	/* Test for no name or clipboard */
	if(!*NAME || !curfile)
		return keysave();

	redraw_statuswin("Saving...");
	if(file_saveas(FP, NAME, (lock_files?FILE_LOCK:0)|FILE_ALLOWOVERWRITE, FILE_MODE666)) {
		if(errno == EAGAIN) {
			redraw_statuswin("%s", strerror(errno));
			return ERR;
		}
		/* Only happens if the filesystem's locking is broken */
		if(file_saveas(FP, NAME, FILE_ALLOWOVERWRITE, S_IRUSR|S_IWUSR)) {
			redraw_statuswin("%s", strerror(errno));
			return ERR;
		}
		redraw_statuswin("Warning - Could not lock file: %s", strerror(errno));
		sleep(1);
	}
	MODIFIED = 0;

	if(snprintf(tmpstr, sizeof(tmpstr), "%s.changes", NAME) >= sizeof(tmpstr)) return OK;
	if(stat(tmpstr, &sb)) {
		redraw_statuswin("file");
		return OK;
	}
	redraw_statuswin("Remove %s (you prob'ly should)?  ", tmpstr);
	tmpchar = 0;
	if(kgetstr(statuswin, &tmpchar, 1, "YyNn") == OK && (tmpchar == 'Y' || tmpchar == 'y')) {
		unlink(tmpstr);
	}

	redraw_statuswin("file");
	return OK;
}

int keyclose() {
	int tmpint;
	char tmpchr;

	if(curfile == -1) return ERR;
	if(curfile == 0) {
		redraw_statuswin("Cannot close clipboard.");
		return ERR;
	}
	if(finfo[curfile].modified) {
		if(NAME[0])
			redraw_statuswin("Save %s before closing (Yes/No/Quick)?  ", NAME);
		else
			redraw_statuswin("Save before closing (Yes/No)?  ");
		tmpchr = 0;
		if(kgetstr(statuswin, &tmpchr, 1, "YyNnQq") == ERR || !tmpchr) {
			redraw_statuswin("file");
			return ERR;
		}
		if(tmpchr == 'y' || tmpchr == 'Y') {
			if(keysave() == ERR) return ERR;
		}
		if(tmpchr == 'Q' || tmpchr == 'q') {
			if(keyqsave() == ERR) return ERR;
		}
	}
	file_close(FP);
	FP = 0;
	werase(WIN);
	wnoutrefresh(WIN);
	delwin(WIN);
	*NAME = 0;

	curfile = -1;
	for(tmpint = 1; tmpint < PUFF_MAX_FILES; tmpint++)
		if(finfo[tmpint].fp) curfile = tmpint;
	redraw_statuswin("file");
	return OK;
}

int keyexit() {
	int tmpint;
	char tmpchar;

	tmpint = 1;		/* exit flag */
	for(curfile = 1; curfile < PUFF_MAX_FILES; curfile++) {
		if(FP && MODIFIED) {
			redrawwin(WIN);
			wnoutrefresh(WIN);
			doupdate();
			if(NAME[0])
				redraw_statuswin("Save %s before exiting (Yes/No/Quick)?  ", NAME);
			else
				redraw_statuswin("Save before exiting (Yes/No)?  ", NAME);
			tmpchar = 0;
			if(kgetstr(statuswin, &tmpchar, 1, "YyNnQq") == ERR || !tmpchar) {
				redraw_statuswin("file");
				tmpint = 0;
				break;
			}
			if(tmpchar == 'Y' || tmpchar == 'y') {
				if(keysave() == ERR) {
					tmpint = 0;
					break;
				}
			}
			if(tmpchar == 'Q' || tmpchar == 'q') {
				if(keyqsave() == ERR) {
					tmpint = 0;
					break;
				}
			}
		}
	}
	if(!tmpint) return ERR;
	doupdate();
	for(curfile = 1; curfile < PUFF_MAX_FILES; curfile++) {
		if(FP) {
			file_close(FP);
			FP = 0;
			werase(WIN);
			wnoutrefresh(WIN);
			delwin(WIN);
		}
	}
	curfile = 0;
	if(save_clipboard) {
		file_save(FP);
	}
	file_close(FP);
	werase(statuswin);
	wrefresh(statuswin);
	delwin(statuswin);
	endwin();
	exit(0);
}

int keyswitch() {
	int tmpint;
	char tmpchr;

	if(curfile == -1) return ERR;
	redraw_statuswin("Which file?  ");
	if(kgetstr(statuswin, &tmpchr, 1, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == ERR || !tmpchr) {
		redraw_statuswin("file");
		return ERR;
	}
	if(tmpchr >= '0' && tmpchr <= '9')
		tmpint = tmpchr - '0';
	else if(tmpchr >= 'a' && tmpchr <= 'z')
		tmpint = tmpchr - 'a' + 10;
	else if(tmpchr >= 'A' && tmpchr <= 'Z')
		tmpint = tmpchr - 'A' + 10;
	else { redraw_statuswin("file"); return ERR; }
	if(!finfo[tmpint].fp) {
		redraw_statuswin("Invalid file %i.", tmpint);
		return ERR;
	}
	curfile = tmpint;
	redraw_statuswin("file");
	return OK;
}

int keyfilemenu() {
	int topfile = 0, maxy, maxx, tmpint, key;
	struct stat sb;
	char permstr[32];

	if(!(messagewin = newwin(LINES-1,COLS,0,0))) {
		redraw_statuswin("Couldn't make message window - shouldn't happen.");
		return ERR;
	}
	getmaxyx(messagewin, maxy, maxx);

	while(1) {
		werase(messagewin);
		wmove(messagewin, 0, 5);            waddstr(messagewin, "Filename");
		wmove(messagewin, 0, maxx - 43);    waddstr(messagewin, "Perms");
		wmove(messagewin, 0, maxx - 32);    waddstr(messagewin, "Memory");
		wmove(messagewin, 0, maxx - 25);    waddstr(messagewin, "Last saved date");

		tmpint = topfile - 1;
		while(++tmpint < PUFF_MAX_FILES && tmpint + 1 - topfile < maxy) {
			wmove(messagewin, tmpint + 1 - topfile, 0);
			if(tmpint < 10) key = tmpint + '0';
			else key = tmpint + 'A' - 10;

			if(tmpint == curfile) waddch(messagewin, '#');
			else if(finfo[tmpint].fp && finfo[tmpint].modified) waddch(messagewin, '!');
			else waddch(messagewin, ' ');
			wprintw(messagewin, "%c - ", key);
			if(!finfo[tmpint].fp) continue;

			if(*finfo[tmpint].name) {
				wprintw(messagewin, "%s", finfo[tmpint].name);
			} else {
				wprintw(messagewin, "/* New file */");
			}

			wmove(messagewin, tmpint + 1 - topfile, maxx - 44);
			waddch(messagewin, '\n');

			wmove(messagewin, tmpint + 1 - topfile, maxx - 32);
			wprintw(messagewin, "%i", file_memusage(finfo[tmpint].fp));

			if(file_stat(finfo[tmpint].fp)) {
 				wmove(messagewin, tmpint + 1 - topfile, maxx - 43);
				text_perms(permstr, file_stat(finfo[tmpint].fp)->st_mode);
				waddstr(messagewin, permstr);

 				wmove(messagewin, tmpint + 1 - topfile, maxx - 25);
				waddstr(messagewin, ctime(& file_stat(finfo[tmpint].fp)->st_mtime ));
			}
		}
		if(wmove(messagewin, maxy, 0) == OK)
//does this work?
			waddstr(messagewin, "# = current file, ! = file was modified");
		wnoutrefresh(messagewin);

//build prompt string and character acceptance string

		redraw_statuswin("^N=New, ^O=Open, ^C=Close, ^S=Save, ^A=More, 0-9, A-Z=Switch:  ");
		wclrtoeol(statuswin);
		while((key = wgetch(statuswin)) == ERR) {
// This isn't right!
			if(fstat(STDIN_FILENO, &sb)) {
				werase(messagewin);
				wnoutrefresh(messagewin);
				delwin(messagewin);
				messagewin = 0;
				raise(SIGTERM);
			}
		}

		if(key == 1) {     /* Ctrl-A */
			topfile += maxy - page_skip - 1;
			if(topfile >= PUFF_MAX_FILES) topfile = 0;
			continue;
		}
		break;
	}

	werase(messagewin);
	wnoutrefresh(messagewin);
	delwin(messagewin);
	if(curfile != -1) { redrawwin(WIN); wnoutrefresh(WIN); }
	messagewin = 0;

	     if(key == 14) keynew();   /* Ctrl-N */
	else if(key == 15) keyopen();  /* Ctrl-O */
	else if(key ==  3) keyclose(); /* Ctrl-C */
	else if(key == 19) keysave();  /* Ctrl-S */
	else if(key >= '0' && key <= '9') {
		tmpint = key - '0';
		if(!finfo[tmpint].fp) {
			redraw_statuswin("Invalid file %i.", tmpint);
		} else {
			curfile = tmpint;
			redraw_statuswin("file");
		}
	} else if(toupper(key) >= 'A' && toupper(key) <= 'Z') {
		tmpint = toupper(key) - 'A' + 10;
		if(!finfo[tmpint].fp) {
			redraw_statuswin("Invalid file %i.", tmpint);
		} else {
			curfile = tmpint;
			redraw_statuswin("file");
		}
	} else {
		redraw_statuswin("Cancelled.");
	}
	return OK;
}

int keykill() {
	off_t tmpoff, tmpoff2;
	int tmpint;

	if(curfile == -1) return ERR;
	tmpoff = file_findline(FP, TOPLINE + VCURSOR); /* beginning of cut */
	tmpoff2 = file_findline(FP, TOPLINE + VCURSOR + 1); /* end of cut */
	if(tmpoff2 == -1) tmpoff2 = file_size(FP);
	tmpint = tmpoff2 - tmpoff;	/* translation: size = end - begin */
	file_delete(FP, tmpoff, tmpint);
	display_file(curfile);
	HCURSOR = REALH = 0;
	MODIFIED = 1;
	return OK;
}

int keycut() {
	off_t tmpoff, tmpoff2;
	int tmpint;
	void *ptr;

	if(curfile == -1) return ERR;
	tmpoff = file_findline(FP, TOPLINE + VCURSOR); /* beginning of cut */
	tmpoff2 = file_findline(FP, TOPLINE + VCURSOR + 1); /* end of cut */
	if(tmpoff2 == -1) tmpoff2 = file_size(FP);
	tmpint = tmpoff2 - tmpoff;	/* translation: size = end - begin */
	if(tmpint < 0) {
		redraw_statuswin("tmpint < 0 - Shouldn't happen.");
		return ERR;
	}
	if(curfile == 0) {
		redraw_statuswin("Don't cut from the clipboard.");
		return ERR;
	}
	if(!(ptr = malloc(tmpint))) {
		redraw_statuswin("Out of memory.");
		return ERR;
	}
	tmpint = file_read(FP, tmpoff, tmpint, ptr);
	if(file_insert(finfo[0].fp, finfo[0].offset, tmpint, ptr) != tmpint) {
		free(ptr);
		redraw_statuswin("Error: %s", strerror(errno));
		return ERR;
	}
	file_delete(FP, tmpoff, tmpint);
	free(ptr);
	HCURSOR = REALH = 0;
	MODIFIED = 1;
	finfo[0].modified = 1;
	seekbyte(0, finfo[0].offset + tmpint);
	display_file(0);
	display_line(0, -1);
	display_file(curfile);
	return OK;
}

int keypaste() {
	int tmpint;
	void *ptr;

	if(curfile == -1) return ERR;
	if(curfile == 0) {
		redraw_statuswin("Don't paste on the clipboard.");
		return ERR;
	}
	if(!(ptr = malloc(file_size(finfo[0].fp)))) {
		redraw_statuswin("Out of memory.");
		return ERR;
	}
	tmpint = file_read(finfo[0].fp, 0, file_size(finfo[0].fp), ptr);
	tmpint = file_insert(FP, OFFSET, tmpint, ptr);
	free(ptr);

	seekbyte(curfile, OFFSET + tmpint);
	display_file(curfile);
	MODIFIED = 1;
	return OK;
}

int keyclearclip() {
	if(curfile == -1) return ERR;

	file_truncate(finfo[0].fp, 0);
	finfo[0].topline = 0;
	finfo[0].vcursor = 0;
	finfo[0].hcursor = 0;
	finfo[0].realh = 0;
	finfo[0].offset = 0;
	finfo[0].modified = 0;
	display_file(0);
	return OK;
}

int main(int Argc, char **Argv) {
	int key, tmpint;
	char tmpstr[MAX_COLS+1];
	struct stat sb;

	argc = Argc;
	argv = Argv;

	if(!(funcarray = malloc(sizeof(void *) * (KLAST - KFIRST)))) {
		printf("Out of memory.\n");
		exit(1);
	}

	/* Initialize all to keynothing first to make sure we don't miss one. */
	tmpint = KLAST - KFIRST;
	while(tmpint--)
		funcarray[tmpint] = keynothing;

	funcarray[KOPEN      - KFIRST] = keyopen;
	funcarray[KREDRAW    - KFIRST] = keyredraw;
	funcarray[KEXIT      - KFIRST] = keyexit;
	funcarray[KCLOSE     - KFIRST] = keyclose;
	funcarray[KSWITCH    - KFIRST] = keyswitch;
	funcarray[KDOWN      - KFIRST] = keydown;
	funcarray[KUP        - KFIRST] = keyup;
	funcarray[KLEFT      - KFIRST] = keyleft;
	funcarray[KRIGHT     - KFIRST] = keyright;
	funcarray[KBOL       - KFIRST] = keybol;
	funcarray[KEOL       - KFIRST] = keyeol;
	funcarray[KPPAGE     - KFIRST] = keyppage;
	funcarray[KNPAGE     - KFIRST] = keynpage;
	funcarray[KBACKSPACE - KFIRST] = keybackspace;
	funcarray[KINSERT    - KFIRST] = keynothing;
	funcarray[KDELETE    - KFIRST] = keydelete;
	funcarray[KQSAVE     - KFIRST] = keyqsave;
	funcarray[KTAB       - KFIRST] = keynothing;
	funcarray[KENTER     - KFIRST] = keyenter;
	funcarray[KSIZE      - KFIRST] = keynothing;
	funcarray[KCANCEL    - KFIRST] = keynothing;
	funcarray[KCLEARCLIP - KFIRST] = keyclearclip;
	funcarray[KCUT       - KFIRST] = keycut;
	funcarray[KPASTE     - KFIRST] = keypaste;
	funcarray[KSEEKLINE  - KFIRST] = keyseekline;
	funcarray[KSEEKBYTE  - KFIRST] = keyseekbyte;
	funcarray[KSAVE      - KFIRST] = keysave;
	funcarray[KNEW       - KFIRST] = keynew;
	funcarray[KSEARCH    - KFIRST] = keysearch;
	funcarray[KSPECIAL   - KFIRST] = keyspecial;
	funcarray[KFILEMENU  - KFIRST] = keyfilemenu;
	funcarray[KSUSPEND   - KFIRST] = keysuspend;
	funcarray[KKILL      - KFIRST] = keykill;
	funcarray[KKILLBOL   - KFIRST] = keykillbol;
	funcarray[KKILLEOL   - KFIRST] = keykilleol;

	{
		struct passwd *userpwd;
		if((userpwd = getpwuid(getuid())))
			if(strlcpy(homedir, userpwd->pw_dir, sizeof(homedir)) >= sizeof(homedir))
				*homedir = 0;
	}

	for(tmpint = 0; tmpint < PUFF_MAX_FILES; tmpint++) {
		finfo[tmpint].fp = 0;
		finfo[tmpint].win = 0;
	}

	putenv("NCURSES_NO_PADDING=1");

	initscr();
// Can this be done with a SIGWINCH?
	if(COLS < 5 || LINES < 2) {
		endwin();
		printf("Term too small.\n");
		exit(1);
	}
	if(COLS > MAX_COLS) {
		endwin();
		printf("Term too large.\n");
		exit(1);
	}

	signal(SIGHUP,   sigs);
	signal(SIGINT,   sigs);
	signal(SIGTERM,  sigs);
	signal(SIGXCPU,  sigs);
	signal(SIGXFSZ,  sigs);
	signal(SIGWINCH, sigs);
#ifdef DEBUG
	signal(SIGUSR1,  sigs);
#endif

	if(		noecho()                        == ERR  ||
			raw()                           == ERR  ||
			scrollok(stdscr, FALSE)         == ERR  ||
			intrflush(stdscr, FALSE)        == ERR  ||
			meta(stdscr, FALSE)             == ERR  ||
			typeahead(STDIN_FILENO)         == ERR) {
		endwin();
		printf("Could not initialize terminal.\n");
		exit(1);
	}

	if(start_color() == OK) color_term = -1;
	else color_term = 0;

	if(!(statuswin = newwin(1, COLS, LINES-1, 0))          ||
			keypad(statuswin, TRUE)         == ERR ||
			scrollok(statuswin, FALSE)      == ERR) {
		endwin();
		printf("Could not initialize status window.\n");
		exit(1);
	}
	wtimeout(statuswin, 1000);
#ifndef __sun__  /* $@#!!! */
	wattron(statuswin, A_BOLD);
#endif
	if(configure() == ERR) {
		endwin();
		printf("configure() failed.\n");
		exit(1);
	}

	/* create clipboard */
	curfile = -1;
	tmpint = strlcpy(tmpstr, homedir, sizeof(tmpstr));
	strlcpy(tmpstr + tmpint, "/.puff.clipboard", sizeof(tmpstr) - tmpint);
	if((!save_clipboard || openfile(0, tmpstr) == ERR) && newfile(0, 0) == ERR) {
		endwin();
		printf("Could not create clipboard.\n");
		exit(1);
	}
	curfile = 0;
	strcpy(NAME, "Clipboard");
	seekbyte(0, file_size(FP));
	display_file(0);
	redrawwin(stdscr);	/* clear screen */

	while((tmpint = getopt(argc, argv, "a")) != -1) {
		switch(tmpint) {
			case '?':
			default:
				;
		}
	}

	tmpint = 0;    /* Error flag */
	curfile = 0;
	while(argc - optind) {
		if(curfile == PUFF_MAX_FILES - 1) {
			redraw_statuswin("Too many files.");
			break;
		}
		if(openfile(curfile + 1, argv[optind]) == ERR) {
			if(errno == ENOENT && newfile(curfile + 1, argv[optind]) == OK) {
				curfile++;
			} else {
				tmpint = 1;
			}
		} else {
			curfile++;
		}
		optind++;
	}
	if(!curfile) {
		if(newfile(1, 0) == ERR) {
			endwin();
			printf("Could not make a new file.\n");
			exit(1);
		}
		curfile = 1;
	}
	if(!tmpint) redraw_statuswin("file");

	while(1) {
		if(curfile != -1) display_line(curfile, -1);
		redraw_statuswin(0);
		if(curfile != -1) {redrawwin(WIN); wnoutrefresh(WIN);}
		doupdate();

// This isn't right!
		while((key = kgetkey(statuswin)) == ERR) {
			if(fstat(STDIN_FILENO, &sb))
				raise(SIGTERM);
		}

		if(curfile != -1 && ((key > 31 && key < 127) || key == KTAB)) {
			if(key == KTAB) key = '\t';
			HCURSOR = REALH;
			*tmpstr = (char)key;
			if(file_insert(FP, OFFSET, 1, tmpstr) != 1) {
				redraw_statuswin("Internal error:  %s", strerror(errno));
				continue;
			}
			keyright();
			finfo[curfile].modified = 1;
		} else if(key >= KFIRST && key < KLAST) {
			/* This magic line calls the function associated with
			 * the key pressed. */
			funcarray[key - KFIRST]();
		}
	} /* while(1) */

	return 0;
}
