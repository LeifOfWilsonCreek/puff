/* This must be compiled with the math library (-lm) */

// floating point types are not done

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>


int vsnprintf(char *str, size_t size, const char *format, va_list args) {
#	define addch(ch) {if(index < size) str[index++] = (ch); else index++;}
#	define endstr() {if(index < size) str[index] = 0; else if(size) str[size-1] = 0;}
	size_t index = 0;

	/* flags */
	char type,alt,ljust,plus,modifier,capital,expform,is_signed;
	int width, prec, base;
	long long inputll = 0;
	unsigned long long inputull = 0;

	format--;
	while(*++format) {
		if(*format != '%') {
			addch(*format);
			continue;
		}
		format++;

		alt = 0;
		ljust = 0;
		plus = 0;
		capital = 0;
		expform = 0;
		width = -1;
		prec = -1;
		base = -1;

flags:
		switch(*format) {
			case '#':       /* alternate form */
			alt = 1;
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

			case '+':		/* force sign indicator */
			plus = '+';
			format++;
			goto flags;

			case '^':
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
		modifier = 0;
		switch(*format) {
			case 'q':       /* Long long (quad) */
			case 'h':	/* Short */
			case 'l':	/* Long */
			case 'L':       /* Long double */
			modifier = *format++;
		}

		type = *format;
		switch(type) {
		case 'd':       /*   signed decimal int */
		case 'i':       /*   signed decimal int */
			if(modifier == 'q') {
				inputll = va_arg(args, long long);
			} else {
				inputll = va_arg(args, int);
			}
			if(base == -1)
				base = 10;
			is_signed = 1;
			goto number;

		case 'o':       /* unsigned octal   int */
			if(modifier == 'q') {
				inputull = va_arg(args, unsigned long long);
			} else {
				inputull = va_arg(args, unsigned int);
			}
			if(base == -1)
				base = 8;
			is_signed = 0;
			goto number;

		case 'u':       /* unsigned decimal int */
			if(modifier == 'q') {
				inputull = va_arg(args, unsigned long long);
			} else {
				inputull = va_arg(args, unsigned int);
			}
			if(base == -1)
				base = 10;
			is_signed = 0;
			goto number;

		case 'p':
			addch('0');
			addch('x');
			if(width == -1 && prec == -1)
				prec = sizeof(void *) * 2;
			else if(width == 2)
				width--;
			else if(width >= 3)
				width =- 2;
			/* FALLTHROUGH */
		case 'X':       /* unsigned hex     int */
			capital = 1;
		case 'x':       /* unsigned hex     int */
			if(modifier == 'q') {
				inputull = va_arg(args, unsigned long long);
			} else {
				inputull = va_arg(args, unsigned int);
			}
			if(base == -1)
				base = 16;
			is_signed = 0;
			goto number;

		case 's':       /* unsigned char * */
			{
				char *tmps;
				int length, spaces;

				tmps = va_arg(args, char *);
				if(!tmps) tmps = "(null)";

				length = strlen(tmps);
				if(width >= 0 && length > width) length = width;
				spaces = prec - length;
				if(spaces < 0) spaces = 0;

				if(!ljust)
					while(spaces--)
						addch(' ');
				while(length--)
					addch(*tmps++);
				if(ljust)
					while(width > 0)
						addch(' ');
			}
			break;

		case 'n':
			if(modifier == 'q') {
				(*va_arg(args, long long *)) = index;
			} else {
				(*va_arg(args, int *)) = index;
			}
			break;

		case 'c':       /* unsigned char */
			if(!ljust && width > 0) while(--width) addch(' ');
			addch(va_arg(args, int));
			if(ljust && width > 0) while(--width) addch(' ');
			break;

		case '%':
			if(!ljust && width > 0) while(--width) addch(' ');
			addch('%');
			if(ljust && width > 0) while(--width) addch(' ');
			break;



#if 0
			{
				long double input;
				int length, power, loop;
				char sign;
		case 'G':
			capital = 1;
		case 'g':
				if(modifier == 'L') {
					input = va_arg(args, long double);
				} else {
					input = va_arg(args, double);
				}
//				if(input > 
				goto flt;
				goto exp;
		case 'E':
				capital = 1;
		case 'e':
				if(modifier == 'L') {
					input = va_arg(args, long double);
				} else {
					input = va_arg(args, double);
				}

exp:
// Dunno how portable isinf() and isnan() are
				power = 0;
				if(isnan(input)) power = 1;
				if(power || isinf(input)) {
					width -= 3;
					if(!ljust) {
						while(width > 0) {
							addch(' ');
							width--;
						}
					}
					if(power) {
						addch('N');
						addch('a');
						addch('N');
					} else {
						addch('I');
						addch('n');
						addch('f');
					}
					while(width > 0) {
						addch(' ');
						width--;
					}
					break;
				}

				if(prec == -1) prec = 6;
				if(input < 0) {
					sign = '-';
					input = -input;
				} else {
					sign = plus;
				}
				if(sign) width--;

				power = log10(input);
				if(power < 0)
					power--;
				length = prec + 5;      /* [-]1.000e[+-]00 */
				if(power >  99)
					length+=log10(power) - 1;
				if(power < -99)
					length+=log10(-power) - 1;
				if(prec || alt) length++;
				width -= length;
				if(!ljust) {
					while(width > 0) {
						addch(' ');
						width--;
					}
				}
				if(sign)
					addch(sign);
				input /= pow(10, power); /* Start with just the first digit. */
				/* Print first digit */
				addch('0' + (char)input);
				input -= (char)input;
				input *= 10;

				if(prec || alt)
					addch('.');
				loop = 0;
				while(loop < prec) {
					addch('0' + (char)input);
					input -= (char)input;
					input *= 10;
					loop++;
				}
				addch(capital ? 'E' : 'e');
				addch(power<0 ? '-' : '+');
				if(power > -10 && power < 10)
					addch('0');
				if(power == 0) {
					addch('0');
				} else {
					if(power < 0)
						power = -power;
					loop = log10(power);
					loop = pow(10, loop);
					while(loop > 0) {
						addch('0' + (power / loop) % 10);
						loop /= 10;
					}
				}
				while(width > 0) {
					addch(' ');
					width--;
				}
			break;

		case 'f':
				if(modifier == 'L') {
					input = va_arg(args, long double);
				} else {
					input = va_arg(args, double);
				}

flt:
// Dunno how portable isinf() and isnan() are
				power = 0;
				if(isnan(input)) power = 1;
				if(power || isinf(input)) {
					width -= 3;
					if(!ljust) {
						while(width > 0) {
							addch(' ');
							width--;
						}
					}
					if(power) {
						addch('N');
						addch('a');
						addch('N');
					} else {
						addch('I');
						addch('n');
						addch('f');
					}
					while(width > 0) {
						addch(' ');
						width--;
					}
					break;
				}


				if(prec == -1) prec = 6;
				if(input < 0) {
					sign = '-';
					input = -input;
				} else {
					sign = plus;
				}
				if(sign) width--;

				power = log10(input);
				if(power < 0)
					power = 0;
				length = power + prec + 1;      /* the length of the number */
				if(prec || alt) length++;
				width -= length;
				if(!ljust) {
					while(width > 0) {
						addch(' ');
						width--;
					}
				}
				if(sign) addch(sign);
				input /= pow(10, power); /* Start with just the first digit. */
				while(power >= 0) {
					addch('0' + (char)input);
					input -= (char)input;
					input *= 10;
					power--;
				}
				if(prec || alt) addch('.');
				while(prec) {
					addch('0' + (char)input);
					input -= (char)input;
					input *= 10;
					prec--;
				}
				while(width > 0) {
					addch(' ');
					width--;
				}
			}
			break;
#endif

		default:
			addch('%');
			addch('?');
		}
		continue;



/* displays "inputll" or "inputull" in base "base".  "is_signed" indicates
 * whether to use "inputll" or "inputull". */
number:
		{
			int length, spaces, zeros;
			char buf[64], sign, hexspecial = 0;
			const char *charset;

			if(capital)
				charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
			else
				charset = "0123456789abcdefghijklmnopqrstuvwxyz";

			if(is_signed) {
				if(inputll < 0) {
					sign = '-';
					inputll = -inputll;
				} else {
					sign = plus;
				}

				/* Figure out the unformatted number */
				if(inputll || (width && prec)) {
					length = 0;
					do {
						buf[length++] = charset[inputll % base];
						inputll /= base;
					} while(inputll && length < sizeof(buf));
				} else {
					/* POSIX says that displaying 0 with width 0
					 * produces a zero-length number */
					length = 0;
				}
			} else {
				sign = 0;

				/* Figure out the unformatted number */
				if(inputull || (width && prec)) {
					length = 0;
					do {
						buf[length++] = charset[inputull % (unsigned)base];
						inputull /= (unsigned)base;
					} while(inputull && length < sizeof(buf));
				} else {
					/* POSIX says that displaying 0 with width 0
					 * produces a zero-length number.  With BSD,
					 * displaying 0 with precision 0 also does. */
					length = 0;
				}
			}

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
			if(sign && spaces) spaces--;

			/* Special POSIX cases for octal and hex with '#' */
			if(alt) {
				if(type == 'o' && !zeros) {
					zeros = 1;
					if(spaces > 0)
						spaces--;
				} else if(type == 'x' && buf[0] && buf[0] != '0') {
					if(spaces == 1)
						spaces = 0;
					else if(spaces > 1)
						spaces -= 2;
					hexspecial = 1;
				}
			}

			/* Construct the final output */
			if(!ljust)
				while(spaces--)
					addch(' ');
			if(sign) addch(sign);
			if(hexspecial) {
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
		}
	}

	endstr();
	return index;
#	undef addch
#	undef endstr
}

int snprintf(char *str, size_t size, const char *format, ...) {
	va_list args;
	int ret;

	va_start(args, format);
	ret = vsnprintf(str, size, format, args);
	va_end(args);
	return ret;
}
