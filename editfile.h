#ifndef __EDITFILE_H
#define __EDITFILE_H

#include <fcntl.h>      /* For file_open()'s flags parameter */
#include <stdio.h>      /* For the FILE * type in the EDITFILE struct */
#include <sys/types.h>  /* For struct stat */
#include <sys/stat.h>   /* For struct stat */

#ifdef __cplusplus
extern "C" {
#endif

/* int functions return < 0 on failure.  Pointer functions return NULL on
 * failure.  This cannot handle files larger than max of off_t, and cannot
 * store more than max of int (or the max allocatable memory) of modified data
 * without saving. */

typedef struct {
	FILE *fp;
	struct stat fp_stat;
	int flags;
	off_t size;
	off_t *lines;
	int sizeof_lines;
	int alloc_lines; /* Number of elements allocated for *lines */
	void *mods;
} EDITFILE;

#define FILE_KEEPOLD 1024
#define FILE_LOCK 2048
#define FILE_LOCKW (2048|4096)  /* FILE_LOCKW includes FILE_LOCK */
#define FILE_ALLOWOVERWRITE 8192

/* Creates a new file in memory, usually to be file_save()'d later on.  This
 * can also be used for a virtual file that is never saved, in which case
 * file_close() will effectively delete it. */
EDITFILE *file_new();

/* Opens a file for editing, returns NULL and sets errno on failure.  This may
 * fail because of open() errors, fstat() errors, malloc() errors, or if the
 * file to be opened is not a regular file (and not being created).  For flags,
 * choose from O_RDONLY, O_RDWR, O_CREAT, O_EXCL, and FILE_LOCK.  The first four
 * options describe how to open() the file (see open(2)).  FILE_LOCK requires a
 * lock to be obtained - shared for O_RDONLY, exclusive for O_RDWR.  FILE_LOCKW
 * is the same as FILE_LOCK, except it will block until a lock is obtainable
 * instead of returning an error.  Locking may also be done later by using
 * file_fileno() to get the file number.  The mode parameter is passed to open()
 * which will use the umask when creating a file. */
EDITFILE *file_open(char *filename, int flags, mode_t mode);

/* Saves changes to the file if it's open for O_RDWR and leaves the file open.
 * Returns -1 and sets errno on a write error that did not corrupt the file,
 * returns -2 and sets errno on a write error that corrupted the file.  (The
 * latter shouldn't happen except on IO errors or if the device runs out of
 * space on a sparse file.  If it's not a sparse file and it runs out of space,
 * it won't be corrupted and will return -1.  In other words, don't use this
 * with a sparse file.)  This never close()'s the file and so should not disrupt
 * a file lock. */
int file_save(EDITFILE *file);

/* Writes the file to a new name, leaving the old file unmodified.
 * If(flags&FILE_KEEPOLD), the old file stays open and the modifications stay
 *   in memory, else the new file stays open and any more modifications are made
 *   relative to the new file and the new file is open for O_RDWR.
 * If(flags&FILE_LOCK), an error is returned if a write lock cannot be obtained
 *   before writing.  If(flags&FILE_LOCKW), it will block for the lock instead
 *   of returning error.
 * If(flags&FILE_ALLOWOVERWRITE), allow this to act like file_save() if the
 *   inodes for the open file and the specified file are the same.  If this
 *   happens FILE_LOCK(W) has no effect.  Otherwise, return -1 with errno ==
 *   EACCES if you try to overwrite the old file.
 * The mode parameter is passed to open() which will use the umask when creating
 *   a file.
 * This preserves line-offset information, and so is faster than saving,
 *   closing, and reopening if you're using line offsets.
 * Returns 0 on success.  Returns -1 on write error.  This will never corrupt
 *   the file.  If -1 is returned, the old file remains open with the
 *   modifications in memory and a partial file may have been written and should
 *   probably be unlink()'d.  If(flags&FILE_KEEPOLD), this will not disrupt an
 *   existing lock, otherwise, it will only release a lock if successful (since
 *   success requires closing the old file).
 * This can be used for safe writes in place of file_save().  If you are
 *   paranoid enough about the data to do that, you should consider leaving the
 *   old file around for a backup. */
int file_saveas(EDITFILE *file, char *new_filename, int flags, mode_t mode);

/* Handy for mode_t's */
#define FILE_MODE666     (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)

/* Writes a backup of the changes to a file as fast as possible - good if your
 * program is TERM'd or for timed backups or the like.  Returns 0 on success. */
int file_backup(EDITFILE *file, char *backup_name);

/* Abandons any changes made to file and restores the changes from the backup
 * file specified.  This can cause very strange things if the original file has
 * been modified since the last backup or if the backup wasn't for that file. */
int file_restore(EDITFILE *file, char *backup_name);

/* Closes the file, and thereby releasing any file locks, and frees all
 * resources used by the object, losing changes.  Use file_save() or
 * file_saveas() to save your changes first. */
void file_close(EDITFILE *file);

/* Returns the new size of the file */
#define file_size(a) ((a) ? (a)->size : -1)

/* Returns the associated read access file descriptor.  Returns -1 if the file
 * is not yet associated with a real file (happens when created with file_new()
 * and hasn't been saved). */
#define file_fileno(a) (((a) && (a)->fp) ? fileno((a)->fp) : -1)

/* To avoid re-fstat()'ing the file every two seconds, a stat struct is
 * maintained by the library.  This may be used by the following macro.  Do not
 * modify the returned struct.  This returns an address to the stat struct.
 * if no file is associated with this object, NULL is returned. */
#define file_stat(a) ((a) && (a)->fp ? &(a)->fp_stat : 0)

/* Returns true if writable, false (zero) if readonly */
/* Logically:  if a, then if (open write stream or new file) */
#define file_writable(a) ((a) && (((a)->flags)&O_ACCMODE) == O_RDWR)

/* fills buf with up to nbytes bytes and returns the number of bytes read.
 * Does not null-terminate, and stops only for EOF. */
int file_read(EDITFILE *file, off_t offset, size_t nbytes, void *buf);

/* Handy function to read a single character */
int file_getc(EDITFILE *file, off_t offset);

/* Changes, inserts, or deletes a single character at offset and updates the
 * size of the file returned by file_size. */
#define FILE_CHANGE 0
#define FILE_INSERT 1
#define FILE_DELETE 2
#define file_change(a,b,c,d) file_write((a),(b),(c),(d),FILE_CHANGE)
#define file_insert(a,b,c,d) file_write((a),(b),(c),(d),FILE_INSERT)
#define file_delete(a,b,c)   file_write((a),(b),(c),(0),FILE_DELETE)
int file_write(EDITFILE *file, off_t offset, size_t nbytes, void *buf, int action);

/* truncates the file in memory to offset - does not extend the file.  Returns
 * the number of bytes truncated or -1 on failure. (0 indicates the file was
 * already truncated at that size.) */
off_t file_truncate(EDITFILE *file, off_t new_size);

/* Finds the offset of line linenum.  Note that by nature this requires looping
 * over the file up to linenum while counting newline's. This is only done once.
 * After finding where a line starts once, it maintains offsets for it and all
 * preceding lines.  This takes sizeof(off_t) * linenum bytes of memory, so be
 * prudent.  Closing the file frees this, of course.  Returns -2 on error, -1
 * on EOF, and an offset on success.  Line numbers start at 0. */
off_t file_findline(EDITFILE *file, int linenum);

/* Returns the number of lines in the file.  This calls file_findline(), and
 * stores pointers to -all- the lines in the file, so if it's a very large file
 * be careful. */
int file_numlines(EDITFILE *file);

/* Returns the number of the line containing the offset passed.  This also calls
 * file_findline(). */
int file_findoff(EDITFILE *file, off_t offset);

/* Search file for thingy, with length size, starting at begin, ending at end.
 * Returns -1 on not-found, or the offset of the beginning of the matching
 * string.  If flags&FILE_NOCASE, use a case insensitive search. */
#define FILE_NOCASE 1
off_t file_search(EDITFILE *file, void *thingy, size_t size, off_t begin, off_t end, int flags);

/* Perform a regular expression search on the file...
 * size is the maximum length of the matching expression (this must be
 * allocatable)
 * Returns -2 if the regular expression is invalid, -1 if it is not found, and
 * the offset of the beginning if it is found. */
off_t file_research(EDITFILE *file, void *thingy, size_t size, off_t begin, off_t end, size_t max_len, int flags);

/* Long-running programs that make many mods may want to call this sometimes.
 * It'll usually free some unneeded memory.  Outwardly, it changes nothing. */
void file_cleanup(EDITFILE *file);

/* Returns the approximate amount of memory (in bytes) allocated for
 * modifications to a file */
int file_memusage(EDITFILE *file);

/* Dump debug data to stdout...pretty useless without knowing the internals.
 * Do not depend on this function. */
void file_debug_dump(EDITFILE *file);

#ifdef __cplusplus
}
#endif

#endif
