/*	$Id$ */
/*
 * Copyright (c) 1994-1995 Sam Leffler
 * Copyright (c) 1994-1995 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char** argv)
{
    int c, h0, h1;
    int quotestrings = 0;
    int stripnl = 0;
    int inquote = 0;

    while ((c = getopt(argc, argv, "qn")) != -1)
	switch (c) {
	case 'q':	quotestrings = 1; break;
	case 'n':	stripnl = 1; break;
	}
    while ((c = getchar()) != EOF) {
	if (c == '%') {
	    h0 = getchar();
	    if (h0 == EOF)
		return (-1);
	    h1 = getchar();
	    if (h1 == EOF)
		return (-1);
	    if ('0' <= h0 && h0 <= '9')
		h0 -= '0';
	    else
		h0 = 10 + (h0 - 'A');
	    if ('0' <= h1 && h1 <= '9')
		h1 -= '0';
	    else
		h1 = 10 + (h1 - 'A');
	    c = 16*h0 + h1;
	    if (c == '"' && quotestrings)
		putchar('\\');
	    putchar(c);
	} else if (c == '+') {
	    putchar(' ');
	} else if (quotestrings && c == '=') {
	    putchar('=');
	    putchar('"');
	    inquote = 1;
	} else if (inquote && c == '&') {
	    putchar('"');
	    putchar(';');
	    inquote = 0;
	} else if (c == '\r') {
	    c = getchar();
	    putchar('\n');
	    if (c != '\n' && c != EOF)
		putchar(c);
	} else if (c != '\n' || !stripnl) {
	    putchar(c);
	}
    }
    if (inquote) {
	putchar('"');
	putchar(';');
    }
    putchar('\n');
    return (0);
}
