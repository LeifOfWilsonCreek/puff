#define KFIRST 512
#define KOPEN 512
#define KREDRAW 513
#define KEXIT 514
#define KCLOSE 515
#define KSWITCH 516
#define KDOWN 517
#define KUP 518
#define KLEFT 519
#define KRIGHT 520
#define KBOL 521
#define KEOL 522
#define KPPAGE 523
#define KNPAGE 524
#define KBACKSPACE 525
#define KINSERT 526
#define KDELETE 527
#define KQSAVE 528
#define KTAB 529
#define KENTER 530
#define KSIZE 531
#define KCANCEL 532
#define KCLEARCLIP 533
#define KCUT 534
#define KPASTE 535
#define KSEEKLINE 536
#define KSEEKBYTE 537
#define KSAVE 538
#define KNEW 539
#define KSEARCH 540
#define KSPECIAL 541
#define KFILEMENU 542
#define KSUSPEND 543
#define KKILL 544
#define KKILLBOL 545
#define KKILLEOL 546

#ifdef DEBUG
#	define KDEBUG 550
#endif
#define KLAST 551

extern int keys[1024][10], definedkeys;

int strmatch(char *s1, char *s2);
void tab_file(char *string, int maxsize);
int kgetkey(WINDOW *win);
int kgetstr(WINDOW *win, char *buf, size_t maxsize, char *validchars);
