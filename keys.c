#include <curses.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include "keys.h"

/* the first int is the return value, the rest are what escape string will
 * match it. Escape strings are ended with -1. */
int keys[sizeof(keys) / sizeof(keys[0])][sizeof(keys[0]) / sizeof(keys[0][0])];
int definedkeys = 0;

/* Call after wgetch() returns ERR to tell whether the terminal is gone. After
 * a timeout, stdin has nothing to read. At EOF or on error (such as after a
 * hangup), stdin stays readable but wgetch() still returns ERR. A key that
 * arrives just after the timeout is pushed back with ungetch(). */
int input_gone(WINDOW *win) {
	fd_set readfds;
	struct timeval tv = { 0, 0 };
	int ready, key;

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
	if(ready == -1) return errno != EINTR;
	if(ready == 0) return 0;
	if((key = wgetch(win)) == ERR) return 1;
	ungetch(key);
	return 0;
}

int kgetkey(WINDOW *win) {
	int thiskey[10], loop, match = 1, found = 1;

	if((thiskey[match] = wgetch(win)) == ERR) return ERR;
	if(thiskey[match] > 31 && thiskey[match] < 127) return thiskey[match];
	while(found) {
		found = 0;
		for(loop = 0; loop < definedkeys; loop++) {
			if(!memcmp(keys[loop]+1, thiskey+1, match*sizeof(int))) {
				if(keys[loop][match+1] == -1) return keys[loop][0];
				found = 1;
			}
		}
		match++;
		if(found) while((thiskey[match] = wgetch(win)) == ERR) {
			if(input_gone(win)) {
				return ERR;
			}
		}
	}

	return ERR;
}

/* returns the index of the first mismatch in the strings. If they totally
 * match, it returns the index of the '\0'. */
int strmatch(char *s1, char *s2) {
	int index = 0;
	while(s1[index] && s1[index] == s2[index]) index++;
	return index;
}

/* tries to extend string to a matching filename with at most maxsize or
 * PATH_MAX bytes, whichever is smaller. */
void tab_file(char *string, int maxsize) {
	DIR *dir;
	struct dirent *entry;
	char tmpstr[PATH_MAX], *ptr, *dirname, *filename, newname[PATH_MAX] = "";
	int filenamelen, found = 0;

	if(maxsize > PATH_MAX) maxsize = PATH_MAX;
	strlcpy(tmpstr, string, sizeof(tmpstr));
	if((ptr = strrchr(tmpstr, '/'))) {
		*ptr = 0;
		dirname = tmpstr;
		if(!*dirname) dirname = "/";
		filename = ptr + 1;
	} else {
		dirname = ".";
		filename = tmpstr;
	}
	filenamelen = strlen(filename);

	if(!(dir = opendir(dirname))) return;
	while((entry = readdir(dir))) {
		if(!strncmp(filename, entry->d_name, filenamelen)) {
			if(!found++) {
				strlcpy(newname, entry->d_name, sizeof(newname));
			} else {
				newname[strmatch(newname, entry->d_name)] = 0;
			}
		}
	}
	closedir(dir);

	if(found) {
		struct stat sb;
		strlcpy(string+(filename-tmpstr), newname, maxsize-(filename-tmpstr));
		if(found == 1 && (filenamelen = strlen(string)) < maxsize - 1 &&
					!stat(string, &sb) && S_ISDIR(sb.st_mode)) {
			string[filenamelen++] = '/';
			string[filenamelen] = 0;
		}
	}
	return;
}

/* if validchars is NULL, it accepts 32-126. If maxsize = 1, one character is
 * accepted and it does not wait for enter, and does not null-terminate.
 * Otherwise, it accepts maxsize-1 bytes from the user and null-terminates the
 * string. Newline is never stored. If validchars is "file", it enables the
 * tab key to finish typing partial filenames and accepts valid filename
 * characters. The data stored in buf is used for the default entry, so it
 * should not be garbage. */
int kgetstr(WINDOW *win, char *buf, size_t maxsize, char *validchars) {
	int end, position, key, file = 0, number = 0, search = 0, beginpos, max_x, cur_y, searchlen = 0;
	char lastsearch[1024];

	getmaxyx(win, cur_y, max_x);
	getyx(win, cur_y, beginpos);

	if(maxsize > max_x - beginpos) maxsize = max_x - beginpos;
	if(maxsize <= 0) return ERR;
	if(validchars && !strcasecmp(validchars, "file")) {
		file = 1;
		validchars = 0;
	}
	if(validchars && !strcasecmp(validchars, "number")) {
		number = 1;
		validchars = 0;
	}
	if(validchars && !strcasecmp(validchars, "search")) {
		search = 1;
		validchars = 0;
		strlcpy(lastsearch, buf, sizeof(lastsearch));
		searchlen = strlen(lastsearch);
		buf[0] = 0;
	}
	if(maxsize == 1) {
		wclrtoeol(win);
		end = position = 0;
	} else {
		buf[maxsize - 1] = 0;		/* overflow protection */
		wclrtoeol(win);
		waddstr(win, buf);
		end = position = strlen(buf);
	}
	while(1) {
		wmove(win, cur_y, beginpos + position);
		wrefresh(win);
		while((key = kgetkey(win)) == ERR) {
			if(input_gone(win)) {
				return ERR;
			}
		}
		if((validchars && strchr(validchars, key))
				|| (!validchars && !number && key >= 32 && key <= 126)
				|| (number && key >= '0' && key <= '9')
/* hex codes */	|| (number && position == 1 && buf[0] == '0' && (key == 'x' || key == 'X'))
				|| (number && position > 1 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X') && key >= 'a' && key <= 'f')
				|| (number && position > 1 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X') && key >= 'A' && key <= 'F')
			) {
			if(end < maxsize - 1) {
				memmove(buf + position + 1, buf + position, end - position + 1);
				winsch(win, key);
				buf[position] = key;
				position++;
				end++;
			} else if(maxsize == 1) {
				winsch(win, key);
				wrefresh(win);
				buf[0] = key;
				return OK;
			}
		} else switch(key) {
		case KENTER:
			return OK;
		break;
		case KCANCEL:
			return ERR;
		break;
		case KBACKSPACE:
			if(position) {
				position--;
				wmove(win, cur_y, beginpos + position);
				wdelch(win);
				memmove(buf + position, buf + position + 1, end-position + 1);
				end--;
			}
		break;
		case KDELETE:
			if(end - position) {
				wdelch(win);
				memmove(buf + position, buf + position + 1, end - position + 1);
				end--;
			}
		break;
		case KKILL:
			end = position = *buf = 0;
			wmove(win, cur_y, beginpos);
			wclrtoeol(win);
		break;
		case KLEFT:
			if(position) position--;
		break;
		case KRIGHT:
			if(end - position) position++;
		break;
		case KBOL:
			position = 0;
		break;
		case KEOL:
			position = end;
		break;
		case KTAB:
			if(!file) break;
			tab_file(buf, maxsize);
			wmove(win, cur_y, beginpos);
			wclrtoeol(win);
			waddstr(win, buf);
			position = end = strlen(buf);
		break;
		case KSEARCH:
			if(!search) break;
			if(searchlen+end >= maxsize-2) break;
			memmove(buf+position+searchlen, buf+position, end-position+1);
			memcpy(buf+position, lastsearch, searchlen);
			end += searchlen;
			waddstr(win, buf+position);
			position += searchlen;
		break;
		case KREDRAW:
			wrefresh(curscr);
			wmove(win, cur_y, beginpos);
			wclrtoeol(win);
			waddstr(win, buf);
		break;
#ifdef DEBUG
		case KDEBUG:
			printf("beginpos=%i pos=%i end=%i str=%s\n", beginpos, position, end, buf);
		break;
#endif
		}
	} /* while(1) */
}
