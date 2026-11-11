#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editfile.h"

int main() {
	EDITFILE *file;
	char input[128];

	while(1) {
		printf("==> ");
		if(!fgets(input, sizeof(input), stdin)) {
			printf("\n");
			break;
		}

		if(!strcmp(input, "help\n")) {
			printf("new, open, save, saveas, savecopy, close, size, fileno,\n");
			printf("writable, read, getc, write, truncate, findline, numlines,\n");
			printf("findoff, search, research, cleanup, memusage, debug, quit.\n");

		} else if(!strcmp(input, "new\n")) {
			file = file_new();

		} else if(!strncmp(input, "open ", 5)) {
			input[strlen(input)-1] = 0;
			file = file_open(input+5, O_RDONLY, 0);
			if(!file) printf("%s\n", strerror(errno));

		} else if(!strcmp(input, "save\n")) {
			printf("%i %s\n", file_save(file), strerror(errno));

		} else if(!strncmp(input, "saveas ", 7)) {
			input[strlen(input)-1] = 0;
			printf("%i %s\n", file_saveas(file, input+7, FILE_LOCK|FILE_ALLOWOVERWRITE, FILE_MODE666), strerror(errno));

		} else if(!strncmp(input, "savecopy ", 9)) {
			input[strlen(input)-1] = 0;
			printf("%i %s\n", file_saveas(file, input+9, FILE_LOCK|FILE_ALLOWOVERWRITE|FILE_KEEPOLD, FILE_MODE666), strerror(errno));

		} else if(!strcmp(input, "close\n")) {
			if(file) {
				file_close(file);
				file = 0;
			}

		} else if(!strcmp(input, "size\n")) {
			printf("%qi\n", file_size(file));

		} else if(!strcmp(input, "fileno\n")) {
			printf("%i\n", file_fileno(file));

		} else if(!strcmp(input, "writable\n")) {
			printf("%i\n", file_writable(file));

		} else if(!strncmp(input, "read ", 5)) {
			char *tmp;
			off_t offset = strtol(input+5, &tmp, 0);
			size_t size = strtol(tmp, &tmp, 0);
			char readstr[size+1];
			readstr[file_read(file, offset, size, readstr)] = 0;
			printf("%s\n", readstr);

		} else if(!strcmp(input, "read\n")) {
			printf("read offset size\n");

		} else if(!strncmp(input, "getc ", 5)) {
			int tmpint = file_getc(file, atoi(input+5));
			if(tmpint == -1) printf("-1\n");
			else printf("%c\n", tmpint);

		} else if(!strncmp(input, "write ", 6)) {
			char *tmp;
			off_t offset = strtol(input+5, &tmp, 0);
			size_t size = strtol(tmp, &tmp, 0);
			int action = strtol(tmp, &tmp, 0);
			tmp++;
			tmp[strlen(tmp)-1] = 0;
			printf("%i\n", file_write(file, offset, size, tmp, action));

		} else if(!strcmp(input, "write\n")) {
			printf("write offset size action string\n");
			printf("action: 0 = change, 1 = insert, 2 = delete\n");

		} else if(!strncmp(input, "truncate ", 9)) {
			off_t tmpoff = strtoul(input+9, 0, 0);
			printf("%qi %qi\n", tmpoff, file_truncate(file, tmpoff));

		} else if(!strncmp(input, "findline ", 9)) {
			off_t tmpint = file_findline(file, atoi(input+9));
			if(tmpint == -1) printf("-1\n");
			else printf("%qi\n", tmpint);

		} else if(!strcmp(input, "numlines\n")) {
			printf("%i\n", file_numlines(file));

		} else if(!strncmp(input, "findoff ", 8)) {
			int tmpint = file_findoff(file, strtol(input+8, 0, 0));
			if(tmpint == -1) printf("-1\n");
			else printf("%i\n", tmpint);

		} else if(!strncmp(input, "search ", 7)) {
			printf("%qi\n", file_search(file, input+7, strlen(input+7), 0, 0, 0));

		} else if(!strcmp(input, "cleanup\n")) {
			file_cleanup(file);

		} else if(!strcmp(input, "memusage\n")) {
			printf("%i\n", file_memusage(file));

		} else if(!strcmp(input, "debug\n")) {
			file_debug_dump(file);

		} else if(!strcmp(input, "quit\n")) {
			exit(0);

		}
	}
	return 0;
}
