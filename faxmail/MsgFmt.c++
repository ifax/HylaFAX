/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
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
#include "MsgFmt.h"
#include "StackBuffer.h"
#include "TextFormat.h"

#include <ctype.h>

MsgFmt::MsgFmt()
{
}

MsgFmt::MsgFmt(const MsgFmt& other)
    : boldFont(other.boldFont)
    , italicFont(other.italicFont)
    , headToKeep(other.headToKeep)
#ifdef notdef
    , headMap(other.headMap)
#endif
{
    headerStop = other.headerStop;
    verbose = other.verbose;
}

MsgFmt::~MsgFmt()
{
}

const fxStr*
MsgFmt::findHeader(const fxStr& name) const
{
    for (u_int i = 0, n = fields.length(); i < n; i++)
	if (strcasecmp(fields[i], name) == 0)
	    return (&headers[i]);
    return (NULL);
}

fxStr
MsgFmt::mapHeader(const fxStr& name)
{
    for (fxStrDictIter hi(headMap); hi.notDone(); hi++)
	if (strcasecmp(hi.key(), name) == 0)
	    return (hi.value());
    return (name);
}

bool
MsgFmt::getLine(FILE* fd, fxStackBuffer& buf)
{
    buf.reset();
    for (;;) {
	int c = getc(fd);
	if (c == EOF)
	    return (buf.getLength() > 0);
	c &= 0xff;
	if (c == '\n')
	    break;
	buf.put(c);
    }
    return (true);
}

/*
 * This function replaces comments with a single white space.
 * Unclosed comments are automatically closed at end of string.
 * Stray closing parentheses are left untouched, as are other invalid chars.
 * Headers which can contain quoted strings should not go through this
 * revision of this function as is doesn't honnor them and could end up doing
 * the wrong thing.
 */
fxStr
MsgFmt::stripComments(const fxStr& s)
{
    fxStr q;
    u_int depth = 0;
    bool wasSpace = true;
    for (u_int i = 0; i < s.length(); i++) {
        switch (s[i]) {
            case '(':
                depth++;
                break;
            case ')':
                if (depth > 0)
                    depth--;
                break;
            case '\\':
                if (depth == 0) {
                    q.append(s[i++]);     // Don't decode them at this time
                    q.append(s[i]);
                    wasSpace = false;
                } else
                  i++;
                break;
            default:
                if (depth == 0) {
                    if (!isspace(s[i]) || !wasSpace) {       // Trim consecutive spaces
                        q.append(s[i]);
                        wasSpace = isspace(s[i]);
                    }
                }
                break;
        }
    }
    while (q.length() > 0 && isspace(q[q.length()-1]))
      q.remove(q.length()-1, 1);      // Trim trailing white space
    return q;
}

void
MsgFmt::parseHeaders(FILE* fd, u_int& lineno)
{
    fxStackBuffer buf;
    fxStr field;				// current header field
    while (getLine(fd, buf)) {
	lineno++;
	if (buf.getLength() == 0)
	    break;
	/*
	 * Collect field name in a canonical format.
	 * If the line begins with whitespace, then
	 * it's the continuation of a previous header.
	 */ 
	fxStr line(&buf[0], buf.getLength());
	u_int len = line.length();
	while (len > 0 && isspace(line[line.length()-1])) {
	    line.remove(line.length()-1, 1);    // trim trailing whitespace
	    len--;
	}
	if (len > 0 && !isspace(line[0])) { 
	    u_int l = 0;
	    field = line.token(l, ':');
	    if (field != "" && l < len) {	// record new header
		fields.append(field);
		// skip leading whitespace
		for (; l < len && isspace(line[l]); l++)
		    ;
		headers.append(line.tail(len-l));
		if (verbose)
		    fprintf(stderr, "HEADER %s: %s\n"
			, (const char*) fields[fields.length()-1]
			, (const char*) headers[headers.length()-1]
		    );
	    }
	} else if (field != "")  {		// append continuation
	    headers[headers.length()-1].append("\n" | line);
	    if (verbose)
		fprintf(stderr, "+HEADER %s: %s\n"
		    , (const char*) field
		    , (const char*) line
		);
	}
    
    }

    /*
     * Scan envelope for any meta-headers that
     * control how formatting is to be done.
     */
    for (u_int i = 0, n = fields.length();  i < n; i++) {
	const fxStr& field = fields[i];
	if (strncasecmp(field, "x-fax-", 6) == 0)
	    setConfigItem(&field[6], headers[i]);
    }
}

void
MsgFmt::setupConfig()
{
    verbose = false;
    boldFont = "Helvetica-Bold";
    italicFont = "Helvetica-Oblique";

    headToKeep.resize(0);
    headToKeep.append("To");
    headToKeep.append("From");
    headToKeep.append("Subject");
    headToKeep.append("Cc");
    headToKeep.append("Date");

    for (fxStrDictIter iter(headMap); iter.notDone(); iter++)
	headMap.remove(iter.key());
}

#undef streq
#define	streq(a,b)	(strcasecmp(a,b)==0)

bool
MsgFmt::setConfigItem(const char* tag, const char* value)
{
    if (streq(tag, "headers")) {
        char* cp = strcpy(new char[strlen(value) + 1], value);
        char* tp;
        do {
            tp = strchr(cp, ' ');
            if (tp) {
                *tp++ = '\0';
            }
            if (streq(cp, "clear")) {
                headToKeep.resize(0);
            } else {
                headToKeep.append(cp);
            }
        } while ((cp = tp));
        delete [] cp;
    } else if (streq(tag, "mapheader")) {
	char* tp = (char *) strchr(value, ' ');
	if (tp) {
	    for (*tp++ = '\0'; isspace(*tp); tp++)
		;
	    headMap[value] = tp;
	}
    } else if (streq(tag, "boldfont")) {
	boldFont = value;
    } else if (streq(tag, "italicfont")) {
	italicFont = value;
    } else if (streq(tag, "verbose")) {
	verbose = FaxConfig::getBoolean(tag);
    } else
	return (false);
    return (true);
}
#undef streq

u_int
MsgFmt::headerCount(void)
{
    u_int nl = 0;
    for (u_int i = 0, n = headToKeep.length(); i < n; i++)
	if (findHeader(headToKeep[i]))
	    nl++;				// XXX handle wrapped lines
    return (nl);
}

#ifdef roundup
#undef roundup
#endif
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

bool
MsgFmt::formatHeaders(TextFormat& fmt)
{
    /*
     * Calculate tab stop for headers based on the
     * maximum width of the headers we are to keep.
     */
    const TextFont* bold = fmt.getFont("Bold");
    if (!bold)
	bold = fmt.addFont("Bold", boldFont);
    headerStop = 0;
    u_int i;
    u_int nHead = headToKeep.length();
    for (i = 0; i < nHead; i++) {
	TextCoord w = bold->strwidth(mapHeader(headToKeep[i]));
	if (w > headerStop)
	    headerStop = w;
    }
    headerStop += bold->charwidth(':');
    TextCoord boldTab = 8 * bold->charwidth(' ');
    headerStop = roundup(headerStop, boldTab);

    /*
     * Format headers we want; embolden field name
     * and italicize field value.  We wrap long
     * items to the field tab stop calculated above.
     */
    u_int nl = 0;
    for (i = 0; i < nHead; i++) {
	const fxStr* value = findHeader(headToKeep[i]);
	if (value) {
	    fmt.beginLine();
		TextCoord hm = bold->show(fmt.getOutputFile(),
		    mapHeader(headToKeep[i]) | ":");
		fmt.hrMove(headerStop - hm);
		showItalic(fmt, *value);
	    fmt.endLine();
	    nl++;
	}
    }
    if (nl > 0) {
	/*
	 * Insert a blank line between the envelope and the
	 * body.  Note that we ``know too much here''--we
	 * know to insert whitespace below to insure valid
	 * PostScript is generated (sigh).
	 */
	fmt.beginLine();
	    fputc(' ', fmt.getOutputFile());	// XXX whitespace needed
	fmt.endLine();
	return true;
    }
    return false;
}

/*
 * Display the string in italic, wrapping to the
 * field header tab stop on any line overflows.
 */
void
MsgFmt::showItalic(TextFormat& fmt, const char* cp)
{
    const TextFont* italic = fmt.getFont("Italic");
    if (!italic)
	italic = fmt.addFont("Italic", italicFont);
    while (isspace(*cp))			// trim leading whitespace
	cp++;
    TextCoord x = fmt.getXOff();		// current x position on line
    FILE* tf = fmt.getOutputFile();		// output stream
    const char* tp = cp;
    for (; *tp != '\0'; tp++) {
	TextCoord hm = italic->charwidth(*tp);
	if (*tp == '\n' || x+hm > fmt.getRHS()) {// text would overflow line
	    italic->show(tf, cp, tp-cp), cp = tp;// flush pending text
	    if (!fmt.getLineWrapping())		// truncate line, don't wrap
		return;
	    fmt.endLine();			// terminate line
	    fmt.beginLine();			// begin another line
	    fmt.hrMove(headerStop);		// reposition to header stop
	    x = fmt.getXOff();
	    if (*tp == '\n') {			// remove leading white space
		for (tp++; isspace(*tp); tp++)
		    ;
		cp = --tp;
	    }
	}
	x += hm;
    }
    if (tp > cp)
	italic->show(tf, cp, tp-cp);		// flush remainder
}
