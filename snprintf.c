/* This must be compiled with the math library (-lm) for pow() and log() */

// The e, E, g, and G types are unimplemented

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#if defined(__linux__) || defined(__sun__)
#	define STACK_SHORT int  /* THIS SUCKS! */
#else
#	define STACK_SHORT short
#endif

#define addch(ch) {if(index < size) str[index++] = (ch); else index++;}
#define endstr() {if(index < size) str[index] = 0; else if(size) str[size-1] = 0;}

int vsnprintf(char *str, size_t size, const char *format, va_list args) {
	size_t index = 0;

	/* flags */
	char type,alt,alt_hex,ljust,plus,modifier,capital,is_signed;
	int width, prec, base;

	format--;
	while(*++format) {
		if(*format != '%') {
			addch(*format);
			continue;
		}
		format++;

		alt = 0;
		alt_hex = 0;
		ljust = 0;
		plus = 0;
		modifier = 0;
		capital = 0;
		is_signed = 0;
		width = -1;
		prec = -1;
		base = -1;

flags:
		switch(*format) {
		case '#':       /* alternate form */
			alt++;  /* twice with hex means prepend 0x to zero-values */
			format++;
			goto flags;

		case '-':       /* left-justify */
			ljust = 1;
			format++;
			goto flags;

		case ' ':       /* leave space before positive */
			if(!plus) plus = ' ';
			format++;
			goto flags;

		case '+':       /* force sign indicator */
			plus = '+';
			format++;
			goto flags;

		case '^':       /* use capital character set */
			capital = 1;
			format++;
			goto flags;
		}

		/* width */
		if(*format >= '0' && *format <= '9') {
			width = 0;
			while(*format >= '0' && *format <= '9')
				width = width * 10 + *format++ - '0';
		}

		/* precision */
		if(*format == '.') {
			prec = 0;
			while(*++format >= '0' && *format <= '9')
				prec = prec * 10 + *format - '0';
		}

		/* base */
		if(*format == '_') {
			base = 0;
			while(*++format >= '0' && *format <= '9')
				base = base * 10 + *format - '0';
		}

		/* conversion modifiers */
modifiers:
		switch(*format) {
		case 'q':       /* Long long (quad_t) */
			modifier = 'q';
			format++;
			goto modifiers;
		case 'l':	/* Long */
			if(modifier == 'l')
				modifier = 'q';
			else
				modifier = 'l';
			format++;
			goto modifiers;
		case 'h':	/* Short */
			modifier = 'h';
			format++;
			goto modifiers;
		case 'L':       /* Long (for double) */
			modifier = 'l';
			format++;
			goto modifiers;
		}

		type = *format;
		switch(type) {
		case 'd':       /*   signed decimal int */
		case 'i':       /*   signed decimal int */
			if(base == -1)
				base = 10;
			is_signed = 1;
			goto integer;

		case 'o':       /* unsigned octal   int */
			if(base == -1)
				base = 8;
			goto integer;

		case 'u':       /* unsigned decimal int */
			if(base == -1)
				base = 10;
			goto integer;

		case 'p':       /* pointer */
			type = 'x';
			alt = 2;
			if(prec == -1)
				prec = sizeof(void *) * 2;
			/* FALLTHROUGH */
		case 'X':       /* unsigned hex     int */
			capital = 1;
			/* FALLTHROUGH */
		case 'x':       /* unsigned hex     int */
			alt_hex = alt;
			if(base == -1)
				base = 16;
			goto integer;

		case 's':       /* string */
			{
				const char *tmps;
				int length, spaces;

				tmps = va_arg(args, const char *);
				if(!tmps) tmps = "(null)";

				/* Width is the minimum number of characters.
				 * Precision is the maximum. */

				length = 0;
				if(prec >= 0)
					/* Can't use strlen because it might not be null-term'd */
					while(length < prec && tmps[length]) length++;
				else
					length = strlen(tmps);

				if(width > length)
					spaces = width - length;
				else
					spaces = 0;

				if(!ljust)
					while(spaces--)
						addch(' ');
				if(capital) {
					while(length--)
						addch(toupper(*tmps++));
				} else if(alt) {
					while(length--)
						addch(tolower(*tmps++));
				} else {
					while(length--)
						addch(*tmps++);
				}
				if(ljust)
					while(spaces--)
						addch(' ');
			}
			continue;

		case 'n':
			if(modifier == 'q') {
				(*va_arg(args, long long *)) = index;
			} else if(modifier == 'l') {
				(*va_arg(args, long *)) = index;
			} else if(modifier == 'h') {
				(*va_arg(args, short *)) = index;
			} else {
				(*va_arg(args, int *)) = index;
			}
			continue;

		case 'c':       /* unsigned char */
			if(!ljust && width > 0) while(--width) addch(' ');
			addch(va_arg(args, int));
			if(ljust && width > 0) while(--width) addch(' ');
			continue;

		case '%':
			if(!ljust && width > 0) while(--width) addch(' ');
			addch('%');
			if(ljust && width > 0) while(--width) addch(' ');
			continue;

		case 'f':
			{
				long double input;
				int length, exp, spaces;
				const char *charset;
				char sign;

				if(capital)
					charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
				else
					charset = "0123456789abcdefghijklmnopqrstuvwxyz";

				if(modifier == 'l')
					input = va_arg(args, long double);
				else
					input = va_arg(args, double);

				if(input != input) {
					/* True for NaN */
					if(!ljust && width > 3) {
						spaces = width - 3;
						while(spaces--)
							addch(' ');
					}
					addch('N');
					addch('a');
					addch('N');
					if(ljust && width > 3) {
						spaces = width - 3;
						while(spaces--)
							addch(' ');
					}
					continue;
				} else if(input == 1.0 / 0.0) {
					/* True for Inf */
					if(!ljust && width > 3) {
						spaces = width - 3;
						while(spaces--)
							addch(' ');
					}
					addch('I');
					addch('n');
					addch('f');
					if(ljust && width > 3) {
						spaces = width - 3;
						while(spaces--)
							addch(' ');
					}
					continue;
				} else if(input == -1.0 / 0.0) {
					/* True for -Inf */
					if(!ljust && width > 4) {
						spaces = width - 4;
						while(spaces--)
							addch(' ');
					}
					addch('-');
					addch('I');
					addch('n');
					addch('f');
					if(ljust && width > 4) {
						spaces = width - 4;
						while(spaces--)
							addch(' ');
					}
					continue;
				}

				if(input < 0) {
					sign = '-';
					input = -input;
				} else {
					sign = plus;
				}

				if(base == -1)
					base = 10;

				if(prec == -1)
					prec = 6;

				if(input < base)
					exp = 0;
				else
					exp = log(input) / log(base);


				/* Figure the length of the number */
				/* If there's a sign, add one to length */
				if(sign)
					length = 1;
				else
					length = 0;

				/* Plus the length of the integer part */
				length += exp + 1;

				/* If there's a decimal point, add one to length */
				if(prec || alt)
					length++;

				/* Plus the fractional part's length */
				length += prec;

				if(width > length)
					spaces = width - length;
				else
					spaces = 0;


				/* Now generate the string */
				if(!ljust)
					while(spaces--)
						addch(' ');
				if(sign)
					addch(sign);

				/* Generate the integer-part */
				input /= pow(base, exp); /* Normalize */
				while(exp >= 0) {
					addch(charset[(int)input]);
					input -= (char)input;
					input *= base;
					exp--;
				}

				if(prec || alt) addch('.');

				/* Generate the fraction-part */
				while(prec--) {
					addch(charset[(int)input]);
					input -= (char)input;
					input *= base;
				}

				if(ljust)
					while(spaces--)
						addch(' ');
			}
			continue;
		default:
			addch('?');
		}
		continue;

integer:
		{
			/* buf must be able to accomidate a long-long in binary, plus a little */
			char buf[sizeof(long long) * 8 + 5], sign;
			const char *charset;
			int length, spaces, zeros;

			if(capital)
				charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
			else
				charset = "0123456789abcdefghijklmnopqrstuvwxyz";

			if(is_signed) {
				long long input;

				if(modifier == 'q') {
					input = va_arg(args, long long);
				} else if(modifier == 'l') {
					input = va_arg(args, long);
				} else if(modifier == 'h') {
					input = va_arg(args, STACK_SHORT);
				} else {
					input = va_arg(args, int);
				}

				if(input < 0) {
					sign = '-';
					input = -input;
				} else {
					sign = plus;
				}

				/* Generate the digit string in reverse */
				length = 0;
				if(input) {
					while(input && length < sizeof(buf)) {
						buf[length++] = charset[input % base];
						input /= base;
					}

					/* Octal in alternate format means prepend a zero */
					if(alt && type == 'o')
						buf[length++] = '0';
				} else {
					/* POSIX says that displaying 0 with width 0
					 * produces a zero-length number.  BSD uses
					 * precision for that instead.  Is my POSIX
					 * documentation wrong? */
					if(prec)
						buf[length++] = '0';
					/* zero with # does not get 0x prepended, but
					 * zero with ## does. */
					if(alt_hex)
						alt_hex--;
				}
			} else {
				unsigned long long input;

				if(modifier == 'q') {
					input = va_arg(args, unsigned long long);
				} else if(modifier == 'l') {
					input = va_arg(args, unsigned long);
				} else if(modifier == 'h') {
					input = va_arg(args, unsigned STACK_SHORT);
				} else {
					input = va_arg(args, unsigned int);
				}

				sign = 0;

				/* Generate the digit string in reverse */
				length = 0;
				if(input) {
					while(input && length < sizeof(buf)) {
						buf[length++] = charset[input % base];
						input /= base;
					}

					/* Octal in alternate format means prepend a zero */
					if(alt && type == 'o')
						buf[length++] = '0';
				} else {
					/* POSIX says that displaying 0 with width 0
					 * produces a zero-length number.  BSD uses
					 * precision for that instead.  Is my POSIX
					 * documentation wrong? */
					if(prec)
						buf[length++] = '0';
					/* zero with # does not get 0x prepended, but
					 * zero with ## does. */
					if(alt_hex)
						alt_hex--;
				}
			}
			/* sign and alt_hex are done below because they come before zero-padding */

			/* If there's a sign, it subtracts from width */
			if(sign && width > 1)
				width--;

			/* Calculate how many spaces and zeros to pad */
			if(prec > length) {
				zeros = prec - length;
				spaces = width - prec;
				if(spaces < 0)
					spaces = 0;
			} else {
				zeros = 0;
				spaces = width - length;
				if(spaces < 0)
					spaces = 0;
			}

			/* Special case for prepending 0x */
			if(alt_hex) {
				if(spaces == 1)
					spaces = 0;
				else if(spaces > 1)
					spaces -= 2;
			}

			/* Construct the final output */
			if(!ljust)
				while(spaces--)
					addch(' ');
			if(sign)
				addch(sign);
			/* Special case for prepending 0x */
			if(alt_hex) {
				addch('0');
				addch(type);
			}
			while(zeros--)
				addch('0');
			while(length--)
				addch(buf[length]);
			if(ljust)
				while(spaces--)
					addch(' ');
			continue;
		}
	}

	endstr();
	return index;
}
#undef addch
#undef endstr

int snprintf(char *str, size_t size, const char *format, ...) {
	va_list args;
	int ret;

	va_start(args, format);
	ret = vsnprintf(str, size, format, args);
	va_end(args);
	return ret;
}
