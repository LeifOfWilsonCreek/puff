/* This must be compiled with the math library (-lm) for pow() and log() if
 * floating point support is compiled in */

/* Define DOUBLE if pow() and log() are available. */
#define DOUBLE

/* If LONG_DOUBLE is not defined, long doubles will be converted to doubles
 * before they are processed.  If it is defined, they are supported normally.
 * Don't define this yet, see bug below. */
//#define LONG_DOUBLE

/*

The format string is based on (and compatible with) POSIX's printf's format
string.  Features which do not appear in POSIX are marked "not POSIX".  The
format is defined as follows:

Characters besides "%" are copied to the output.  "%" indicates a field, defined
by the characters that follow it.  Those characters may be, in the following
order:

 - Zero or more of "#", "+", " ", "-", or "^".
    - "#": Use the "alternate" format, which depends on the field type
    - "+": A plus sign should be placed before signed numeric field types
    - " ": A space should be placed before signed numeric field types
      (plus overrides space)
    - "-": Output should be left-justified in the field's width
      (translate: negative-width)
    - "^": All letters in the output should be upper-case (not POSIX)

 - Optional decimal digit string specifying the field's minimum width.  If the
   total width of the field is less than this number, it is padded with spaces.
   Substituting '*' for the digit string causes an int argment to be read as the
   width.  ('*' is not POSIX)  Specifying a width never causes a field to be
   truncated.

 - Optional period followed by a decimal digit string specifying the field's
   precision.  The default and interpretation of the precision depends on the
   field type.  A period with no digit string specifies precision of zero.
   Substituting '*' for the digit string causes an int argment to be read as the
   precision.  ('*' is not POSIX)

 - Optional underline followed by a decimal digit string specifying a numeric
   field's base.  The base of any field type may be changed using this modifier.
   Substituting '*' for the digit string causes an int argment to be read as the
   base.  (this field is not POSIX)

 - One optional type modifier.  These may be used to modify the numeric type
   parameter or the numeric type pointed to by the parameter.
    - "h":  The integer type is changed to a short int or unsigned short int
    - "l":  The integer type is changed to a long int or unsigned long int
    - "ll": The integer type is changed to a long long or unsigned long long
    - "q":  The integer type is changed to a quad_t or u_quad_t
    - "L":  The floating point type is changed to a long double
   "ll" and "q" are not POSIX.

 - The field type indicator described below
    - "d" or "i":  Type is int, default base is 10, "#" is meaningless,
      precision is the minimum number of digits.
    - "u":  Same as "i", but type is unsigned int.
    - "o":  Same as "u", but default base is 8, "#" means the first digit must
      be zero.
    - "x":  Same as "u", but default base is 16, "#" means prefix with "0x".
    - "X":  Same as "x", but uses uppercase digits and "0X".
    - "p":  Type is (void *) and may not be overridden, the output format
      depends on who you ask.
    - "s":  Type is (char *) and may not be overridden, "#" (not POSIX) means
      convert to lowercase ("^" still means convert to uppercase - not POSIX),
      precision (not POSIX) is the maximum number of characters to display.
      Precision may be used for showing unterminated strings.
    - "n":  Type is (int *), "#" is meaningless, precision is meaningless.
      Stores the number of characters printed thus far at the location pointed
      to.
    - "c":  Type is int and may not be overridden, "#" is meaningless, precision
      is meaningless.  The integer is converted to (unsigned char), then
      displayed.
    - "f":  Type is double (not float!), default base is 10, "#" means a period
      shows even if precision is zero, precision is the number of digits after
      the period.
    - "%":  Takes no parameter, displays "%" in the same manner as "c" would.
    - "e", "E", "g", or "G":  Not implemented yet


BUGS:
 - Regarding diouxX:
If my documentation is correct, POSIX states that printing zero with width
zero produces "", and precision has undefined meaning.  BSD says width never
truncates a field, and precision is the minimum number of digits to appear,
padding with zeros if necessary.  Printing zero with width zero therefore
produces "0" and printing zero with /precision/ zero produces "".  I did it
the BSD way because it's more regular and I don't think anyone implements
the other way.

 - Regarding s:
If my documentation is correct, POSIX states that the width of a string is
the maximum number of characters and precision has undefined meaning.  BSD
says width never truncates a field, so the width of a string is the /minimum/
number of characters, while the /precision/ is the maximum.  I did it the BSD
way because it's more regular and I don't think anyone implements the other way.

 - Regarding (long double) support:
These are not really supported.  They are converted to doubles before processing
because pow() and log() can only operate on doubles.  It would cause Bad Things
if converting from long double to double produced Inf or -Inf where log() and
pow() are called.

// XXX: fix base < 2 || base > 36
// XXX: what about %#.0o

 */





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
#define endstr() {if(index < size) str[index] = 0; else if(size) str[size - 1] = 0;}

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
		} else if(*format == '*') {
			width = va_arg(args, int);
			if(width < 0) {
				width = -width;
				ljust = 1;
			}
		}

		/* precision */
		if(*format == '.') {
			prec = 0;
			while(*++format >= '0' && *format <= '9')
				prec = prec * 10 + *format - '0';
		} else if(*format == '*') {
			prec = va_arg(args, int);
		}

		/* base */
		if(*format == '_') {
			base = 0;
			while(*++format >= '0' && *format <= '9')
				base = base * 10 + *format - '0';
		} else if(*format == '*') {
			base = va_arg(args, int);
		}

		/* conversion modifiers */
modifiers:
		switch(*format) {
		case 'l':	/* Long */
			if(modifier == 'l') {
				modifier = 'L';  /* Long long */
				format++;
				break;
			}
			modifier = *format;
			format++;
			goto modifiers;
		case 'q':       /* quad_t */
		case 'h':	/* Short */
		case 'L':       /* Long double */
			modifier = *format;
			format++;
		}

		type = *format;
		switch(type) {
		case 'd':       /*   signed decimal int */
		case 'i':       /*   signed decimal int */
			if(base < 2)
				base = 10;
			is_signed = 1;
			goto integer;

		case 'o':       /* unsigned octal   int */
			if(base < 2)
				base = 8;
			goto integer;

		case 'u':       /* unsigned decimal int */
			if(base < 2)
				base = 10;
			goto integer;

		case 'p':       /* pointer */
			type = 'x';
			alt = 2;
			if(prec < 0)
				prec = sizeof(void *) * 2;
			/* FALLTHROUGH */
		case 'X':       /* unsigned hex     int */
			capital = 1;
			/* FALLTHROUGH */
		case 'x':       /* unsigned hex     int */
			alt_hex = alt;
			if(base < 2)
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
				(*va_arg(args, quad_t *)) = index;
			} else if(modifier == 'L') {
				(*va_arg(args, long long *)) = index;
			} else if(modifier == 'l') {
				(*va_arg(args, long *)) = index;
			} else if(modifier == 'h') {
				(*va_arg(args, short *)) = index;
			} else {
				(*va_arg(args, int *)) = index;
			}
			if(width > 0) while(width--) addch(' ');
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
#ifdef LONG_DOUBLE
				long double input;
#else
				double input;
#endif
				int length, exp, spaces;
				const char *charset;
				char sign;

				if(capital)
					charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
				else
					charset = "0123456789abcdefghijklmnopqrstuvwxyz";

				if(modifier == 'L')
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

				if(base < 2)
					base = 10;

				if(prec < 0)
					prec = 6;

				if(input < base)
					exp = 0;
				else
#ifdef LONG_DOUBLE  /* log() on the line below is for doubles */
#warning Long doubles currently cause infinite loops in some cases
#endif
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
#ifdef LONG_DOUBLE  /* pow() on the line below is for doubles */
#warning Long doubles currently cause infinite loops in some cases
#endif
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
					input = va_arg(args, quad_t);
				} else if(modifier == 'L') {
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
					input = va_arg(args, u_quad_t);
				} else if(modifier == 'L') {
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
