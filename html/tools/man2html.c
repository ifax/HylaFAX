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
/*
 * Convert the output of man to HTML.
 */
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

static	const char* beginBold	= "<B>";
static	const char* endBold	= "</B>";
static	const char* beginItalic	= "<I>";
static	const char* endItalic	= "</I>";

#define	TRUE	1
#define	FALSE	0
static	int trace = FALSE;

static	void cvtFile(FILE*, FILE*);
static	void cvtFile1(FILE*, char*, u_int);

int
main(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;
    while ((c = getopt(argc, argv, ":t")) != -1)
	switch (c) {
	case 't':
	    trace = TRUE;
	    break;
	case '?':
	    fprintf(stderr, "usage: [file]\n", argv[0]);
	    return (-1);
	}
    if (optind < argc) {
	FILE* fd = fopen(argv[optind], "r");
	if (fd) {
	    cvtFile(stdout, fd);
	    fclose(fd);
	} else
	    fprintf(stderr, "%s: Can not open.\n", argv[optind]);
    } else {
	cvtFile(stdout, stdin);
    }
    return (0);
}

static void
cvtFile(FILE* out, FILE* in)
{
    u_int totalSize = 0;
    const int bs = 32*1024;
    char* buf = (char*) malloc(bs);
    for (;;) {
	int nb = read(fileno(in), buf + totalSize, bs);
	if (nb <= 0)
	    break;
	totalSize += nb;
	buf = (char*) realloc(buf, totalSize + bs);
    }
    /* shrink the buffer down to its needed size when finished */
    buf = (char*) realloc(buf, totalSize);
    cvtFile1(out, buf, totalSize);
    free(buf);
}

#define	emit(fd, str)		fprintf(fd, "%s", str)

static void
flushText(FILE* fd, const char* buf, int len)
{
    const char* cp = buf; 
    while (len > 0) {
	if (*cp == '<' || *cp == '>' || *cp == '&') {
	    if (cp - buf > 0)
		fprintf(fd, "%.*s", cp-buf, buf);
		buf = cp+1;
	    }
	    switch (*cp) {
	    case '<':	emit(fd, "&lt;"); break;
	    case '>':	emit(fd, "&gt;"); break;
	    case '&':	emit(fd, "&amp;"); break;
	}
	cp++, len--;
    }
    if (cp - buf > 0)
	fprintf(fd, "%.*s", cp-buf, buf);
}

typedef enum {		/* man page output parsing state machine */
    NORM,		/* Not in underline, boldface or whatever mode */
    NORM_B,		/* In normal, last char was \b */
    UNDER,		/* In underline, need next char */
    UL_X,		/* In underline, got non-_, need \b */
    UL_U,		/* In underline, got _, need \b */
    UL_B,		/* In underline, got \b */
    UL_XB,		/* In underline, got char & \b, need _ */
    UL_UB,		/* In underline, got _ & \b, need non-_ */
    BOLD,		/* In boldface, need next char */
    BO_X,		/* In boldface, got char, need \b */
    BO_XB,		/* In boldface, got char & \b, need same char */
    UL_BOLD,		/* In underline+boldface, need next char */
    UL_BO_U,		/* In underline+boldface, have _ */
    UL_BO_X,		/* In underline+boldface, have X */
    UL_BO_UB,		/* In underline+boldface, have _\b */
    UL_BO_XB		/* In underline+boldface, have X\b */
} State;

static const char* stateNames[] = {
    "NORM", "NORM_B",
    "UNDER", "UL_X", "UL_U", "UL_B", "UL_XB", "UL_UB",
    "BOLD", "BO_X", "BO_XB",
    "UL_BOLD", "UL_BO_U", "UL_BO_X", "UL_BO_UB", "UL_BO_XB"
};

static void
traceState(FILE* fd, State s, int col, int c)
{
     fprintf(fd, "%s col %d ", stateNames[s], col);
     if (c == '\b')
	fprintf(fd, "'\\b'\n");
     else if (c == '\t')
	fprintf(fd, "'\\t'\n");
     else if (c == '\n')
	fprintf(fd, "'\\n'\n");
     else
	 fprintf(fd, "'%c'\n", c);
}

#define	BeginFont(fd, ns, bs)	state = ns, emit(fd, bs)
#define	EndFont(fd, bs) { \
    flushText(fd, linebuf, cp-linebuf-1); \
    emit(fd, bs); \
}

static void
cvtFile1(FILE* out, char* data, u_int cc)
{
    int col = 0;			/* current output column */
    int prevLineBlank = FALSE;		/* previous line was empty */
    State state = NORM;			/* parser state */
    char linebuf[2048];			/* XXX no bounds checking */
    char* cp = linebuf;

    while (cc > 0) {
	int c;
	
	c = *data++, cc--;
	if (trace)
	    traceState(out, state, col, c);
	if (c == '\b') {
	    switch (state) {
	    case NORM:	state = NORM_B; continue;
	    case UNDER:	state = UL_B; continue;
	    case UL_X:	state = UL_XB; continue;
	    case UL_U:	state = UL_UB; continue;
	    case BOLD:	state = BO_XB; continue;
	    case BO_X:	state = BO_XB; continue;
	    case UL_BOLD:state = UL_BO_XB; continue;
	    case UL_BO_U:state = UL_BO_UB; continue;
	    case UL_BO_X:state = UL_BO_XB; continue;
	    }
	}
	/*
	 * Advance the state machine.
	 */
	switch (state) {
	case NORM:			/* normal mode */
	    break;
	case NORM_B:			/* normal mode X\b */
	    if (cp == linebuf) {	/* \bX => X (shouldn't happen) */
		state = NORM;
		break;
	    }
	    if (c == cp[-1] || c == '_') {	/* X\bX or X\b_ */
		/*
		 * NB: We convert |\b| to | for tables.
		 */
		if (c != '|') {
		    flushText(out, linebuf, cp-linebuf-1);
		    linebuf[0] = cp[-1], cp = linebuf+1;
		    if (c == '_')
			BeginFont(out, UNDER, beginItalic);
		    else
			BeginFont(out, BOLD, beginBold);
		    continue;
		}
	    }
	    /*
	     * NB: col generates _\b| for table corners; we
	     * discard the underscore since that seems to look
	     * the best when displayed.
	     */
	    if (cp[-1] == '_' && c != '|') {	/* _\bX */
		cp--, col--;			/* replace '_' w/ X */
		flushText(out, linebuf, cp-linebuf);
		cp = linebuf;
		BeginFont(out, UNDER, beginItalic);
	    } else {				/* X\bY => Y (overstrike) */
		cp--, col--;
		state = NORM;
	    }
	    break;
	case UNDER:			/* underlining (got X\b_ or _\bX) */
	    if (c == '_')
		state = UL_U;
	    else if (!isspace(c) && c != '\n')
		state = UL_X;
	    else
		state = UNDER;
	    break;
	case UL_B:			/* underlining, have \b */
	    if (c == cp[-1]) {		/* X\bX */
		BeginFont(out, UL_BOLD, beginBold);
		continue;
	    }
	    EndFont(out, endItalic);
	    cp = linebuf;		/* X\bY => Y (overstrike) */
	    state = NORM;
	    break;
	case UL_X:			/* underlining, have X */
	case UL_U:			/* underlining, have _ */
	    EndFont(out, endItalic);
	    linebuf[0] = cp[-1], cp = linebuf+1;
	    state = NORM;
	    break;
	case UL_XB:			/* underlining, have X\b */
	    if (c == cp[-1]) {		/* X\bX */
		BeginFont(out, UL_BOLD, beginBold);
	    } else if (c != '_') {	/* X\bY */
		EndFont(out, endItalic);
		cp = linebuf;		/* X\bY => Y */
		state = NORM;
	    } else {
		state = UNDER;		/* X\b_ */
	    }
	    continue;
	case UL_UB:			/* underlining, have _\b */
	    if (c == '_') {		/* _\b_ */
		BeginFont(out, UL_BOLD, beginBold);
		continue;
	    }
	    cp--, col--;
	    state = UNDER;		/* _\bX */
	    break;
	case BOLD:			/* emboldening */
	    if (!isspace(c) && c != '\n')
		state = BO_X;
	    break;
	case BO_X:			/* emboldening, have X */
	    EndFont(out, endBold);
	    linebuf[0] = cp[-1], cp = linebuf+1;
	    state = NORM;
	    break;
	case BO_XB:			/* emboldening, have X\b */
	    if (c == cp[-1]) {		/* X\bX */
		state = BOLD;
		continue;
	    }
	    if (c == '_') {		/* X\b_ */
		/*
		 * X\b_ when emboldening could be X\bX\b_ in which
		 * case we migth transition to bold-underline or it
		 * could be X\bX\bY\_ in which case we should switch
		 * to underline.  However, for certain tables, it's
		 * common to find X\bX\b_ or similar due to col
		 * merging a rule mark with the line of text in the
		 * the table.   Since this is the most common case
		 * we simply discard the underscores and keep emboldening.
		 * Additional states would let us isolate this case.
		 */
#ifdef notdef
		linebuf[0] = cp[-1], cp = linebuf+1;
		BeginFont(out, UNDER, beginItalic);
#endif
		state = BOLD;
		continue;
	    }
	    EndFont(out, endBold);
	    if (cp[-1] == '_')		/* _\bX */
		BeginFont(out, UNDER, beginItalic);
	    else
		state = NORM;
	    cp = linebuf;
	    break;
	case UL_BOLD:			/* underline+bold */
	    if (c == '_')
		state = UL_BO_U;
	    else if (!isspace(c) && c != '\n')
		state = UL_BO_X;
	    break;
	case UL_BO_U:			/* underline_bold, have _ */
	case UL_BO_X:			/* underline+bold, have X */
	    EndFont(out, endBold);
	    emit(out, endItalic);
	    linebuf[0] = cp[-1], cp = linebuf+1;
	    state = NORM;
	    break;
	case UL_BO_UB:			/* underline+bold, have _\b */
	    state = UL_BOLD;
	    cp--, col--;		/* overwrite _ */
	    break;
	case UL_BO_XB:			/* underline+bold, have X\b */
	    if (c == cp[-1]) {		/* X\bX */
		state = UL_BOLD;
		continue;
	    }
	    EndFont(out, endBold);
	    if (c == '_') {		/* X\b_ (revert to underline) */
		state = UNDER;
		continue;
	    }
	    emit(out, endItalic);
	    cp = linebuf;		/* X\bY => Y (overstrike) */
	    state = NORM;
	    break;
	}

	if (c == '\t') {		/* convert tabs to spaces */
	    /*
	     * The tabstop setting (8) is hardcoded because
	     * most of our input comes from col which thinks
	     * that tabstops are set every 8 spaces (sigh).
	     */
	    do {
		*cp++ = ' ', col++;
	    } while (col % 8);
	} else if (c == '\n') {		/* line wrap on \n */
	    if (col > 0 || !prevLineBlank) {
		*cp++ = '\n';
		flushText(out, linebuf, cp-linebuf);
	    }
	    prevLineBlank = (col == 0 ? TRUE : FALSE);
	    cp = linebuf;
	    col = 0;
	} else {
	    if (iscntrl(c)) {		/* convert to "^X" */
		*cp++ = '^', col++;
		c = (c == '\177') ? '?' : (c | 0100);
	    }
	    *cp++ = c, col++;
	}
	if (state == NORM) {
	    /* look ahead to process the most common cases */
	    while (cc > 0 && *data != '\b') {
		c = *data++, cc--;
		if (trace)
		    traceState(out, state, col, c);
		if (c == '\t') {
		    do {
			*cp++ = ' ', col++;
		    } while (col % 8);
		} else if (c == '\n') {
		    if (col > 0 || !prevLineBlank) {
			*cp++ = '\n';
			flushText(out, linebuf, cp-linebuf);
		    }
		    prevLineBlank = (col == 0 ? TRUE : FALSE);
		    cp = linebuf;
		    col = 0;
		} else {
		    if (iscntrl(c)) {		/* convert to "^X" */
			*cp++ = '^', col++;
			c = (c == '\177') ? '?' : (c | 0100);
		    }
		    *cp++ = c, col++;
		}
	    }
	}
    }
    flushText(out, linebuf, cp-linebuf);	/* flushText trailing line */
}
