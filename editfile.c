/*
new features needed:
	function to paste in a file
	search function - flags: case sensitive, bol, eol, start, end
	undo function
	file_{gets,puts} should maybe be written to handle text data...

bugs:
	file_saveas() should copy perms to the new file
	file_save() shouldn't loop over data that hasn't changed if overwriting the old
	offset checking at the beginning of file_write(), check size != 0
	store/check file dates and warn about them changing
	searching can be optimized
	line numbers should be unsigned
	check out-of-memory errors
*/

#define MOD_BLOCK_SIZE 16       /* Size multiplier of modifications stored in memory */
#define BLOCK_SIZE 512          /* Block size for writing files...should really get this from stat() */
#define LINES_CHUNK 10000       /* Allocation chunk-size (units = sizeof(off_t) bytes).  The slower realloc() is
                                 * on your system, the larger this should be for load time on large files. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "editfile.h"

typedef struct {		/* modification */
	void *next;
	off_t begin;
	int old_size;
	int new_size;
	int alloc_size;
	unsigned char *data;
} mod_t;

EDITFILE *file_new() {
	EDITFILE *file;
	file = (EDITFILE *)calloc(1, sizeof(EDITFILE));
	if(!file) return 0;

	file->flags = O_RDONLY;
	return file;
}

EDITFILE *file_open(char *filename, int flgs, mode_t mode) {
	EDITFILE *file;
	struct stat sb;
	struct flock lock;
	int tmpfd;

	if((flgs&O_ACCMODE) == O_RDWR) {
		if((tmpfd = open(filename, O_RDWR | (flgs&O_CREAT) | (flgs&O_EXCL), mode)) == -1) return 0;
	} else if((flgs&O_ACCMODE) == O_RDONLY) {
		if((tmpfd = open(filename, O_RDONLY, mode)) == -1) return 0;
	} else {
		errno = EINVAL;  /* Write-only not allowed */
		return 0;
	}

	if(fstat(tmpfd, &sb)) {
		/* Shouldn't happen */
		close(tmpfd);
		return 0;
	}

	if(!S_ISREG(sb.st_mode)) {
		/* They prob'ly tried to open a directory or something. */
		close(tmpfd);
		if(S_ISDIR(sb.st_mode))
			errno = EISDIR;
		else
			errno = EINVAL;
		return 0;
	}

	/* Obtain a lock if necessary */
	if(flgs&FILE_LOCK) {
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_pid = getpid();
		lock.l_type = ((flgs&O_ACCMODE) == O_RDWR)?F_WRLCK:F_RDLCK;
		lock.l_whence = SEEK_SET;

		if(fcntl(tmpfd, ((flgs&FILE_LOCKW) == FILE_LOCKW)?F_SETLKW:F_SETLK, &lock)) {
			close(tmpfd);
			return 0;
		}
	}

	if(!(file = (EDITFILE *)calloc(1, sizeof(EDITFILE)))) {
		close(tmpfd);
		return 0;
	}

	if((flgs&O_ACCMODE) == O_RDWR) {
		if(!(file->fp = fdopen(tmpfd, "rb+"))) {
			/* Shouldn't happen */
			close(tmpfd);
			free(file);
			return 0;
		}
		file->flags = O_RDWR;
	} else {
		if(!(file->fp = fdopen(tmpfd, "rb"))) {
			/* Shouldn't happen */
			close(tmpfd);
			free(file);
			return 0;
		}
		file->flags = O_RDONLY;
	}

	file->flags |= flgs&FILE_LOCKW;
	file->size = sb.st_size;
	file->fp_stat = sb;
	return file;
}

int file_save(EDITFILE *file) {
	off_t readoffset, writeoffset;
	int length, buf_size;
	mod_t *thismod, *nextmod;
	unsigned char *read_buf1, *read_buf2, *tmp;

	if((file->flags&O_ACCMODE) != O_RDWR) {
		errno = EACCES;
		return -1;
	}

	if(!file->mods) {
		futimens(file_fileno(file), NULL);
		return 0;
	}

	/* Extend the end of the file to file->size to be sure there's enough free
	 * space...it is still possable to run out of space when modifying a sparse
	 * file, but that's not officially supported. */
	if(file->fp_stat.st_size < file->size) {
		if(fseek(file->fp, 0, SEEK_END) == -1) return -1;

		for(writeoffset = file->fp_stat.st_size; writeoffset < file->size; writeoffset += 8) {
			if(fputs("AAAAAAAA", file->fp) == EOF) {
				fflush(file->fp);
				if(ftruncate(fileno(file->fp), file->fp_stat.st_size)) return -2;
				else return -1;
			}
		}
		if(fflush(file->fp) == EOF) {
			if(ftruncate(fileno(file->fp), file->fp_stat.st_size)) return -2;
			else return -1;
		}
	}

	/* Find the minimum buffer size - which is the total size of all additions
	 * to the modified data */
	buf_size = 1;
	thismod = (mod_t *)file->mods;
	while(thismod) {
		register int a;
		a = thismod->new_size - thismod->old_size;
		if(a > 0) buf_size += a;
		thismod = (mod_t *)thismod->next;
	}
	buf_size += BLOCK_SIZE - buf_size % BLOCK_SIZE;

	/* Allocate a buffer so we can always be reading ahead of the modifications
	 * we're making - DON'T FORGET TO FREE THIS */
	if(!(read_buf1 = (unsigned char *)malloc(buf_size))) {
		if(ftruncate(fileno(file->fp), file->fp_stat.st_size)) return -2;
		else return -1;
	}
	if(!(read_buf2 = (unsigned char *)malloc(buf_size))) {
		free(read_buf1);
		if(ftruncate(fileno(file->fp), file->fp_stat.st_size)) return -2;
		else return -1;
	}

	/* Actually write out the data - stay a buffer ahead so no writes overlap
	 * the next read. */
	readoffset = writeoffset = ((mod_t *)file->mods)->begin;
	length = file_read(file, readoffset, buf_size, read_buf1);
	readoffset += length;
	while(length == buf_size) {
		length = file_read(file, readoffset, buf_size, read_buf2);
		readoffset += length;

		if(fseek(file->fp, writeoffset, SEEK_SET)) {
			free(read_buf1);
			free(read_buf2);
			return -2;
		}
		if((signed)fwrite(read_buf1, 1, buf_size, file->fp) < buf_size) {
			free(read_buf1);
			free(read_buf2);
			return -2;
		}
		writeoffset += buf_size;
		tmp = read_buf1;
		read_buf1 = read_buf2;
		read_buf2 = tmp;
	}
	if(fseek(file->fp, writeoffset, SEEK_SET)) {
		free(read_buf1);
		free(read_buf2);
		return -2;
	}
	if((signed)fwrite(read_buf1, 1, length, file->fp) != length) {
		free(read_buf1);
		free(read_buf2);
		return -2;
	}
	writeoffset += length;
	free(read_buf1); read_buf1 = 0;
	free(read_buf2); read_buf2 = 0;
	if(fflush(file->fp) == EOF) return -2;
	if(readoffset != file->size || writeoffset != file->size) return -2;
	if(ftruncate(fileno(file->fp), file->size)) return -2;

	/* free mods */
	thismod = (mod_t *)file->mods;
	while(thismod) {
		nextmod = (mod_t *)thismod->next;
		free(thismod->data);
		free(thismod);
		thismod = (mod_t *)nextmod;
	}
	file->mods = 0;

	if(fstat(fileno(file->fp), &file->fp_stat)) {
		/* Shouldn't happen... */
		file->fp_stat.st_size = file->size;
	}

	return 0;
}

int file_saveas(EDITFILE *file, char *new_filename, int flgs, mode_t mode) {
	int tmpfd;
	FILE *out_fp;
	mod_t *thismod, *nextmod;
	off_t offset, read_size;
	struct stat write_stat;
	struct flock lock;
	char buf[BLOCK_SIZE];

	if(!new_filename) {
		errno = EFAULT;
		return -1;
	}

	/* Don't clobber the old file */
	if(file->fp && !stat(new_filename, &write_stat)) {
		struct stat read_stat;
		if(fstat(fileno(file->fp), &read_stat)) {
			/* This shouldn't happen... */
			return -1;
		}
		if(read_stat.st_dev == write_stat.st_dev && read_stat.st_ino == write_stat.st_ino) {
			if(flgs&FILE_ALLOWOVERWRITE) {
				return file_save(file);
			}
			errno = EACCES;
			return -1;
		}
	}

	if((tmpfd = open(new_filename, O_RDWR|O_CREAT, mode)) == -1) return -1;
	if(fstat(tmpfd, &write_stat)) {
		/* This shouldn't happen... */
		close(tmpfd);
		return -1;
	}
	if(!S_ISREG(write_stat.st_mode)) {
		close(tmpfd);
		if(S_ISDIR(write_stat.st_mode))
			errno = EISDIR;
		else
			errno = EINVAL;
		return 0;
	}

	if(flgs&FILE_LOCK) {
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_pid = getpid();
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;

		if(fcntl(tmpfd, ((flgs&FILE_LOCKW) == FILE_LOCKW)?F_SETLKW:F_SETLK, &lock)) {
			close(tmpfd);
			return -1;
		}
	}

	if(ftruncate(tmpfd, 0) || !(out_fp = fdopen(tmpfd, "rb+"))) {
		close(tmpfd);
		return -1;
	}

	offset = 0;
	while((read_size = file_read(file, offset, sizeof(buf), buf))) {
		offset += read_size;
		if((signed)fwrite(buf, 1, read_size, out_fp) < read_size) {
			fclose(out_fp);
			return -1;
		}
	}
	if(fflush(out_fp) == EOF || offset != file->size) {
		fclose(out_fp);
		return -1;
	}

	if(flgs&FILE_KEEPOLD) {
		fclose(out_fp);
		return 0;
	}

	/* free mods */
	thismod = (mod_t *)file->mods;
	while(thismod) {
		nextmod = (mod_t *)thismod->next;
		free(thismod->data);
		free(thismod);
		thismod = (mod_t *)nextmod;
	}
	file->mods = 0;

	/* close old file, keep new file */
	if(file->fp) {
		fclose(file->fp);
	}

	file->fp = out_fp;
	file->flags = O_RDWR | (flgs&FILE_LOCKW);

	if(fstat(fileno(file->fp), &file->fp_stat)) {
		/* Shouldn't happen... */
		file->fp_stat.st_size = file->size;
	}

	return 0;
}

int file_backup(EDITFILE *file, char *backup_name) {
	FILE *fp;
	mod_t *thismod;

	if(!(fp = fopen(backup_name, "w"))) return -1;
	fchmod(fileno(fp), S_IRUSR|S_IWUSR);
	if(fputs("EDITFILE backup v1.0\n", fp) == EOF) { fclose(fp); return -1; }

	/* Write out each mod sequentially */
	thismod = (mod_t *)file->mods;
	while(thismod) {
		if(fwrite(&thismod->begin, sizeof(thismod->begin), 1, fp) != 1) { fclose(fp); return -1; }
		if(fwrite(&thismod->old_size, sizeof(thismod->old_size), 1, fp) != 1) { fclose(fp); return -1; }
		if(fwrite(&thismod->new_size, sizeof(thismod->new_size), 1, fp) != 1) { fclose(fp); return -1; }
		if(fwrite(thismod->data, thismod->new_size, 1, fp) != 1) { fclose(fp); return -1; }
		thismod = (mod_t *)thismod->next;
	}
	if(fclose(fp) == EOF) return -1;

	return 0;
}

int file_restore(EDITFILE *file, char *backup_name) {
	FILE *fp;
	mod_t *lastmod, *thismod, tmpmod;
	char buf[32];
	int diff;

	/* Open file / Check validity */
	if(!(fp = fopen(backup_name, "r"))) return -1;
	if(!fgets(buf, sizeof(buf), fp)) { fclose(fp); errno = EINVAL; return -1; }
	if(strcmp(buf, "EDITFILE backup v1.0\n")) { fclose(fp); errno = EINVAL; return -1; }

	/* Free modifications */
	thismod = (mod_t *)file->mods;
	while(thismod) {
		lastmod = (mod_t *)thismod->next;
		free(thismod->data);
		free(thismod);
		thismod = lastmod;
	}
	file->mods = 0;

	/* Line offsets will be messed up */
	free(file->lines);
	file->lines = 0;
	file->sizeof_lines = 0;
	file->alloc_lines = 0;

	/* Read in each mod sequentially */
	thismod = tmpmod.next = 0;
	diff = 0;
	while(1) {
#define PANIC if(feof(fp)) errno = EINVAL; fclose(fp); return -1;
		if(fread(&tmpmod.begin, sizeof(tmpmod.begin), 1, fp) != 1)          { break; }
		if(fread(&tmpmod.old_size, sizeof(tmpmod.old_size), 1, fp) != 1)    { PANIC }
		if(tmpmod.begin + tmpmod.old_size > file->fp_stat.st_size)          { PANIC }
		if(fread(&tmpmod.new_size, sizeof(tmpmod.new_size), 1, fp) != 1)    { PANIC }
		if(tmpmod.begin < 0 || tmpmod.old_size < 0 || tmpmod.new_size < 0)  { PANIC }
		tmpmod.alloc_size = tmpmod.new_size + MOD_BLOCK_SIZE - tmpmod.new_size % MOD_BLOCK_SIZE;
		if(!(tmpmod.data = (unsigned char *)malloc(tmpmod.alloc_size)))     { PANIC }
		if(fread(tmpmod.data, tmpmod.new_size, 1, fp) != 1)                 { free(tmpmod.data); PANIC }
		diff += tmpmod.new_size - tmpmod.old_size;

		lastmod = thismod;
		if(!(thismod = (mod_t *)malloc(sizeof(mod_t))))                     { free(tmpmod.data); PANIC }
		*thismod = tmpmod;
		if(lastmod) lastmod->next = thismod; else file->mods = thismod;
#undef PANIC
	}

	file->size = file->fp_stat.st_size + diff;
	if(feof(fp)) {
		fclose(fp);
		return 0;
	} else {
		fclose(fp);
		return -1;
	}
}

void file_close(EDITFILE *file) {
	mod_t *mod, *nextmod;
	if(file->fp) fclose(file->fp);
	free(file->lines);

	/* free modifications */
	mod = (mod_t *)file->mods;
	while(mod) {
		nextmod = (mod_t *)mod->next;
		free(mod->data);
		free(mod);
		mod = nextmod;
	}

	free(file);
	return;
}

int file_read(EDITFILE *file, off_t offset, size_t nbytes, void *buf) {
	mod_t *thismod;
	int tmpint;
	size_t bytes_left = nbytes;
	int diff = 0;  /* difference between begin in the new file and old file */
	unsigned char *buff = (unsigned char *)buf;

	if(offset < 0) return -1;
	thismod = (mod_t *)file->mods;
	while(bytes_left && offset < file->size) {
		/* thismod->begin + diff = offset of the beginning of thismod->data.
		 * thismod->begin = offset in the old file */

		/* skip irrelavent mods, but keep track of the offset difference */
		while(thismod && thismod->begin + diff + thismod->new_size <= offset) {
			diff += thismod->new_size - thismod->old_size;
	    thismod = (mod_t *)thismod->next;
		}
		if(thismod && thismod->begin + diff <= offset) {
			/* this byte is part of a mod */
			*buff = thismod->data[offset - (thismod->begin + diff)];
		} else {
			/* this byte not modified - doesn't happen on new files. */
			if(ftell(file->fp) != offset - diff &&
				fseek(file->fp, offset - diff, SEEK_SET) == -1)
					return -1; /* error */
			if((tmpint = fgetc(file->fp)) == -1)
				return -1;	/* error */
			*buff = tmpint;
		}
		buff++;
		offset++;
		bytes_left--;
	}

	return nbytes - bytes_left;    /* return the number of bytes read */
}

int file_getc(EDITFILE *file, off_t offset) {
	unsigned char ret;

	if(file_read(file, offset, 1, &ret) != 1) return -1;
	return ret;
}

int file_write(EDITFILE *file, off_t offset, size_t nbytes, void *buf, int action) {
	/* action is: change (0), insert (1), or delete (2) */
	void *ptr;
	mod_t *nextmod, *thismod, *prevmod = 0;
	int diff = 0;		/* difference between new file and old file */
	off_t linenum = 0;
	size_t bytes_left = nbytes;
	unsigned char *buff = (unsigned char *)buf;

	if(offset < 0) return 0;
	if(action != FILE_INSERT && offset == file->size) return 0;
	if(offset > file->size) return 0;

	if(file->lines && file->lines[file->sizeof_lines] != -1 &&
			file->lines[file->sizeof_lines] <= offset + nbytes)
		file_findoff(file, offset + nbytes > file_size(file)
			? file_size(file) : offset + nbytes);

	nextmod = (mod_t *)file->mods;
	/* skip irrelavent mods, but keep track of the offset difference */
	while(nextmod && nextmod->begin + diff + nextmod->new_size <= offset) {
		/* If offset is right after a mod and there's no mod immediately
		 * following, extend the mod so we don't waste a retarded amount
		 * of space.  We are still functional without the following 4 lines,
		 * just less memory efficient. */
		if(offset == nextmod->begin + diff + nextmod->new_size) {
			if(!nextmod->next) goto Extend;
			if(((mod_t *)(nextmod->next))->begin > nextmod->begin + nextmod->old_size) goto Extend;
		}

		diff += nextmod->new_size - nextmod->old_size;
		prevmod = nextmod;
		nextmod = (mod_t *)nextmod->next;
	}
	/* offset - diff = offset in the old file */
	if(!nextmod || nextmod->begin + diff > offset) {
		/* no mod block exists for this yet */
		if(!(thismod = (mod_t *)malloc(sizeof(mod_t)))) return -1;
		thismod->begin = offset - diff - MOD_BLOCK_SIZE/2;
		if(thismod->begin < 0) thismod->begin = 0;
		if(prevmod && thismod->begin < prevmod->begin + prevmod->old_size)
			thismod->begin = prevmod->begin + prevmod->old_size;
		thismod->alloc_size = MOD_BLOCK_SIZE;
		if(!(thismod->data = (unsigned char *)malloc(thismod->alloc_size))) {
			free(thismod);
			return -1;
		}
		if(file->fp && fseek(file->fp, thismod->begin, SEEK_SET) == -1) {
			free(thismod->data);
			free(thismod);
			return -1;
		}
		if(file->fp) {
			/* alloc_size - 2 because we leave an extra byte at all times. */
			thismod->old_size = fread(thismod->data, 1, thismod->alloc_size-2, file->fp);
		} else {
			thismod->old_size = 0;
		}
		if(nextmod && thismod->old_size > nextmod->begin - thismod->begin) {
			thismod->old_size = nextmod->begin - thismod->begin;
		}
		thismod->new_size = thismod->old_size;
		thismod->next = nextmod;
		if(prevmod) {
			prevmod->next = thismod;
		} else {
			file->mods = thismod;
		}
	} else {
Extend:
		/* mod block already exists */
		thismod = nextmod;
	}
	nextmod = (mod_t *)thismod->next;

	/* thismod is now defined.  prevmod and nextmod may or may not be NULL */

	if(file->lines)
		while(file->lines[linenum] != -1 && file->lines[linenum] <= offset)
			linenum++;
	/* linenum is the first line whose offset was modified */

	if(action == FILE_INSERT) {
		unsigned char *pos;
		/* For inserting, we always extend the current block. */
		while(thismod->alloc_size <= thismod->new_size + (signed)nbytes) {
			/* if alloc_size is too small */
			if(!(ptr = realloc(thismod->data, thismod->alloc_size + MOD_BLOCK_SIZE))) return 0;
			thismod->data = (unsigned char *)ptr;
			thismod->alloc_size += MOD_BLOCK_SIZE;
		}
		pos = thismod->data + offset - thismod->begin - diff;
		/* make room for the new data */
		memmove(pos + nbytes, pos, thismod->begin + diff + thismod->new_size - offset);
		/* actually insert data */
		memcpy(pos, buff, nbytes);
		thismod->new_size += nbytes;
		file->size += nbytes;

		if(file->lines && (file->lines[file->sizeof_lines] == -1 ||
				   file->lines[file->sizeof_lines] > offset)) {
			int line_diff = 0, loop;

			/* How many added newlines? */
			loop = 0;
			while(loop < nbytes)
				if(buff[loop++] == '\n') line_diff++;
			/* line_diff is the number of newlines added */

			/* realloc file->lines */
			loop = file->sizeof_lines + line_diff + 1;
			if(loop > file->alloc_lines) {
				loop += LINES_CHUNK;
				if(!(ptr = realloc(file->lines, sizeof(file->lines[0]) * loop))) return -2;
				file->alloc_lines = loop;
				file->lines = (off_t *)ptr;
			}

			/* move existing ones down */
			memmove(file->lines + linenum + line_diff, file->lines + linenum, (file->sizeof_lines - linenum + 1) * sizeof(file->lines[0]));
			file->sizeof_lines += line_diff;

			/* fix inserted lines */
			loop = 0;
			while(line_diff) {
				offset++;
				if(buff[loop] == '\n') {
					file->lines[linenum++] = offset;
					line_diff--;
				}
				loop++;
			}

			/* increment offsets for following lines */
			while(linenum <= file->sizeof_lines && file->lines[linenum] != -1)
				file->lines[linenum++] += nbytes;
		}

		return nbytes;
	}

	/* (offset - diff) - thismod->begin = (place in old file) - beginning of
	 * mod = index of the offset in thismod->data */
	while(bytes_left && offset < file->size) {
		if(offset >= thismod->begin + diff + thismod->new_size) {
			/* we've gone outside the current mod-block.  We will either
			 * go to the next mod block if it immediately follows this one
			 * or extend the current mod-block as necessary.  We need to
			 * prevent overlapping mod-blocks. */
			while(nextmod && offset >= nextmod->begin + diff + (thismod->new_size-thismod->old_size)) {
				/* go on to next mod-block...there could be 0-byte blocks,
				 * so we use a while loop */
				diff += thismod->new_size - thismod->old_size;
				prevmod = thismod;
				thismod = nextmod;
				nextmod = (mod_t *)nextmod->next;
			}
		}

		if(thismod->new_size >= thismod->alloc_size) {
			/* if alloc_size is too small.  Notice there will be at least one
			 * free char */
			void *ptr;
			if(!(ptr = realloc(thismod->data, thismod->alloc_size + MOD_BLOCK_SIZE))) break;
			thismod->data = (unsigned char *)ptr;
			thismod->alloc_size += MOD_BLOCK_SIZE;
		}

		if(offset >= thismod->begin + diff + thismod->new_size) {
			/* if this is still true, we had no block immediately following,
			 * so we'll extend thismod by one character.  This won't happen
			 * with created files since mods always have something following
			 * them, so the only way to extend a mod is with insert */
			fseek(file->fp, thismod->begin + thismod->old_size, SEEK_SET);
			thismod->data[thismod->new_size] = fgetc(file->fp);
			thismod->old_size++;
			thismod->new_size++;
		}

		if(file->lines && (file->lines[file->sizeof_lines] == -1 ||
				file->lines[file->sizeof_lines] > offset)) {
			if(thismod->data[offset - diff - thismod->begin] == '\n') {
				memmove(file->lines + linenum, file->lines + linenum + 1, (file->sizeof_lines - linenum) * sizeof(file->lines[0]));
				file->sizeof_lines--;
			}
		}
		if(action == FILE_CHANGE) {
			int loop;
			if(file->lines && *buff == '\n' && (file->lines[file->sizeof_lines] == -1
					|| file->lines[file->sizeof_lines] > offset)) {
				/* realloc file->lines */

				loop = file->sizeof_lines + 2;
				if(loop > file->alloc_lines) {
					loop += LINES_CHUNK;
					if(!(ptr = realloc(file->lines, sizeof(file->lines[0]) * loop))) return -2;
					file->alloc_lines = loop;
					file->lines = (off_t *)ptr;
				}

				memmove(file->lines + linenum + 1, file->lines + linenum, (file->sizeof_lines - linenum + 1) * sizeof(file->lines[0]));
				file->lines[linenum] = offset + 1;
				linenum++;
			}
			thismod->data[offset - diff - thismod->begin] = *buff++;
			offset++;
		} else if(action == FILE_DELETE) {
			memmove(thismod->data + offset - diff - thismod->begin, thismod->data + offset - diff - thismod->begin + 1, thismod->begin + diff + thismod->new_size - offset - 1);
			thismod->new_size--;
			file->size--;
		}

		bytes_left--;
	}

	diff = nbytes - bytes_left;
	if(file->lines && action == FILE_DELETE) {
		while(linenum <= file->sizeof_lines && file->lines[linenum] != -1)
			file->lines[linenum++] -= diff;
	}

	return diff;
}

off_t file_truncate(EDITFILE *file, off_t new_size) {
	int diff = 0;
	mod_t *lastmod = 0, *thismod, *nextmod;
	void *ptr;

	if(!file) {
		errno = EFAULT;
		return -1;
	}

	if(file->size < new_size || new_size < 0) {
		errno = EINVAL;
		return -1;
	}

	thismod = (mod_t *)file->mods;
	while(thismod && thismod->begin + diff + thismod->new_size < new_size) {
		diff += thismod->new_size - thismod->old_size;
		lastmod = (mod_t *)thismod;
		thismod = (mod_t *)thismod->next;
	}

	if(thismod && thismod->begin + diff < new_size) {
		/* Truncate inside thismod */
		thismod->old_size = file->fp_stat.st_size - thismod->begin;
		thismod->new_size = new_size - thismod->begin - diff;
		thismod->alloc_size = file->size + MOD_BLOCK_SIZE - file->size % MOD_BLOCK_SIZE;

		/* Doesn't matter much if it fails since it's getting smaller */
		ptr = realloc(thismod->data, thismod->alloc_size);
		if(ptr) thismod->data = (unsigned char *)ptr;

		lastmod = thismod;
		thismod = (mod_t *)thismod->next;
		lastmod->next = 0;
		/* Free all the mods following this one */
		while(thismod) {
			nextmod = (mod_t *)thismod->next;
			free(thismod);
			thismod = nextmod;
		}
	} else {
		/* No mod-block exists here - thismod is past the new end */
		while(thismod) {
			nextmod = (mod_t *)thismod->next;
			free(thismod);
			thismod = nextmod;
		}

		if(lastmod) {
			lastmod->next = 0;
		} else {
			file->mods = 0;
		}
		if(!(thismod = (mod_t *)malloc(sizeof(mod_t)))) return -1;
		thismod->alloc_size = MOD_BLOCK_SIZE;
		if(!(thismod->data = (unsigned char *)malloc(thismod->alloc_size))) { free(thismod); return -1; }
		if(lastmod) {
			lastmod->next = thismod;
		} else {
			file->mods = thismod;
		}

		thismod->next = 0;
		thismod->begin = new_size - diff;
		thismod->new_size = 0;
		thismod->old_size = file->fp_stat.st_size - thismod->begin;
	}

	/* Fix line number offsets if necessary */
	if(file->sizeof_lines) {
		while(file->lines[file->sizeof_lines-1] > new_size) {
			file->sizeof_lines--;
			file->lines[file->sizeof_lines] = -1;
		}
	}

	diff = new_size - file->size;
	file->size = new_size;
	return diff;
}

off_t file_findline(EDITFILE *file, int linenum) {
	if(!file->lines && !(file->lines = (off_t *)malloc(sizeof(file->lines[0])))) return -2;
	file->lines[0] = 0;
	if(linenum < 0) return -1;
	if(linenum <= file->sizeof_lines) {
		/* already stored */
		return file->lines[linenum];
	}
	/* EOF check */
	if(file->lines && file->lines[file->sizeof_lines] == -1) return -1;

	/* find more offsets */
	{
		off_t offset;
		void *ptr;
		int ch;

		offset = file->lines[file->sizeof_lines];  /* last known beginning of a line */
		while(file->sizeof_lines < linenum) {
			/* Realloc() in chunks for speed. */
			if(file->sizeof_lines + 2 > file->alloc_lines) {
				ch = file->sizeof_lines + 2 + LINES_CHUNK;
				if(!(ptr = realloc(file->lines, sizeof(file->lines[0]) * ch))) return -2;
				file->alloc_lines = ch;
				file->lines = (off_t *)ptr;
			}
			file->sizeof_lines++;

			while((ch = file_getc(file, offset)) != '\n' && ch != -1) offset++;
			if(ch == -1) {
				file->lines[file->sizeof_lines] = -1;
				break;
			}
			file->lines[file->sizeof_lines] = ++offset;
		}

		return file->lines[file->sizeof_lines];
	}
}

int file_numlines(EDITFILE *file) {
	int ret;
	if(file->lines && file->lines[file->sizeof_lines] == -1) return file->sizeof_lines;
	ret = file_findline(file, INT_MAX);
	if(ret < 0) return ret;
	return file->sizeof_lines;
}

int file_findoff(EDITFILE *file, off_t offset) {
	int line = 0;
	off_t pq;

	if(offset > file->size) return -1;
	if(offset < 0) return -1;
	while((pq = file_findline(file, line)) <= offset && pq != -1) line++;

	return line - 1;
}

int smartstrncasecmp(const char *s1, const char *s2, size_t len) {
	while(*s1 && len) {
		int diff;
		if(isupper(*s1))
			diff = *s1 - *s2;  /* Uppercase letters in s1 match sensitively */
		else
			diff = *s1 - tolower(*s2);
		if(diff) return diff;
		s1++, s2++, len--;
	}
	return 0;
}

off_t file_search(EDITFILE *file, void *thingy, size_t size, off_t begin, off_t end, int flags) {
	char *buf;
	if(!end) end = file_size(file);
	end -= size;
	if(end < begin || !size) return -1;

	buf = (char *)malloc(size);
	if(flags & FILE_NOCASE) {
		if(file_read(file, begin, size, buf) < (signed)size) { free(buf); return -1; }
		while(begin <= end) {
			if(file_read(file, begin+size-1, 1, buf+size-1) < 1) { free(buf); return -1; }
			if(!strncasecmp(thingy, buf, size)) { free(buf); return begin; }
			begin++;
			memmove(buf, buf+1, size-1);
		}
	} else if(flags & FILE_SMARTCASE) {
		if(file_read(file, begin, size, buf) < (signed)size) { free(buf); return -1; }
		while(begin <= end) {
			if(file_read(file, begin+size-1, 1, buf+size-1) < 1) { free(buf); return -1; }
			if(!smartstrncasecmp(thingy, buf, size)) { free(buf); return begin; }
			begin++;
			memmove(buf, buf+1, size-1);
		}
	} else {
		if(file_read(file, begin, size, buf) < (signed)size) { free(buf); return -1; }
		while(begin <= end) {
			if(file_read(file, begin+size-1, 1, buf+size-1) < 1) { free(buf); return -1; }
			if(!memcmp(thingy, buf, size)) { free(buf); return begin; }
			begin++;
			memmove(buf, buf+1, size-1);
		}
	}
	free(buf); return -1;
}

off_t file_research(EDITFILE *file, void *thingy, size_t size, off_t begin, off_t end, size_t max_size, int flags) {
#ifdef A
	regex_t comppat;	/* compiled pattern */
	int tmpint;

	tmpint = regcomp(&comppat, thingy, REG_EXTENDED);
	if(tmpint) return -2;

	if(!end) end = file_size(file);
	if(end < begin || !size) return -1;




    if(flags&FILE_NOCASE) {
	char buf[size];
	while(begin <= end) {
	    if(file_read(file, begin, size, buf) < size) return -1;
	    if(!strncasecmp(thingy, buf, size)) return begin;
	    begin++;
	}
    } else {
	char buf[size];
	while(begin <= end) {
	    if(file_read(file, begin, size, buf) < size) return -1;
	    if(!memcmp(thingy, buf, size)) return begin;
	    begin++;
	}
    }
#endif
    return -1;
}

void file_cleanup(EDITFILE *file) {
	mod_t *thismod, *nextmod = 0;
	void *ptr;
	int newsize;
	if(!file) return;

	thismod = (mod_t *)file->mods;
	while(thismod) {
		nextmod = (mod_t *)thismod->next;

		if(nextmod && thismod->begin + thismod->old_size == nextmod->begin) {
			/* combine thismod and nextmod if they are adjacent */
			newsize = thismod->new_size + nextmod->new_size;
			newsize = newsize + MOD_BLOCK_SIZE - newsize % MOD_BLOCK_SIZE;
			ptr = realloc(thismod->data, newsize);
			if(ptr) {
				thismod->data = (unsigned char *)ptr;
				thismod->alloc_size = newsize;
				memcpy(thismod->data + thismod->new_size, nextmod->data, nextmod->new_size);
				free(nextmod->data);
				thismod->old_size += nextmod->old_size;
				thismod->new_size += nextmod->new_size;
				thismod->next = (mod_t *)nextmod->next;
				free(nextmod);
				nextmod = (mod_t *)thismod->next;
			} else {
				/* if realloc fails, don't loop forever... */
				thismod = nextmod;
			}
		} else if(thismod->alloc_size > thismod->new_size + MOD_BLOCK_SIZE) {
			/* Shorten excessive amounts of memory */
			thismod->alloc_size = thismod->new_size + MOD_BLOCK_SIZE - thismod->new_size % MOD_BLOCK_SIZE;
			/* Leaves at least one extra byte of space */
			ptr = realloc(thismod->data, thismod->alloc_size);
			if(ptr) thismod->data = (unsigned char *)ptr;
		} else {
			thismod = nextmod;
		}
	}
	return;
}

int file_memusage(EDITFILE *file) {
	int usage = 0;
	mod_t *thismod = (mod_t *)file->mods;

	while(thismod) {
		usage += thismod->alloc_size + sizeof(mod_t);
		thismod = (mod_t *)thismod->next;
	}
	usage += file->alloc_lines * sizeof(file->lines[0]);
	return usage;
}

void file_debug_dump(EDITFILE *file) {
	mod_t *mod;
	int loop;

	mod = (mod_t *)file->mods;
	printf("\033[2Jsize = %i\r\n", (int)file->size);
	while(mod) {
		printf("begin = %i ", (int)mod->begin);
		printf("old_size = %i ", mod->old_size);
		printf("new_size = %i ", mod->new_size);
		printf("alloc_size = %i\r\n", mod->alloc_size);
		mod->data[mod->new_size] = 0;	/* Gosh, good thing there's always at
						 * least an extra byte allocated...What's a Gosh? */
		printf("data = \"%s\"\r\n", mod->data);
		mod = (mod_t *)mod->next;
	}
	return;

	printf("lines: %i\t", file->sizeof_lines);
	for(loop = 0; loop < file->sizeof_lines; loop++)
		printf("%i\t", (int)file->lines[loop]);
	return;
}
