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
#ifndef _MsgFmt_
#define	_MsgFmt_

#include "TextFormat.h"
#include "StrArray.h"
#include "StrDict.h"

class fxStackBuffer;

struct MsgFmt {
    TextCoord	headerStop;		// tab stop for headers
    fxStr	boldFont;		// bold font family name
    fxStr	italicFont;		// italic font family name
    bool	verbose;		// trace header handling

    fxStrArray	fields;			// header tags
    fxStrArray	headers;		// header values
    fxStrArray	headToKeep;		// headers to keep
    fxStrDict	headMap;		// header mapping table

    MsgFmt();
    MsgFmt(const MsgFmt& other);
    virtual ~MsgFmt();

    void setupConfig();
    virtual bool setConfigItem(const char* tag, const char* value);

    static bool getLine(FILE* fd, fxStackBuffer& buf);

    const fxStr* findHeader(const fxStr& name) const;
    u_int headerCount(void);
    fxStr mapHeader(const fxStr& name);
    void parseHeaders(FILE* fd, u_int& lineno);
    void formatHeaders(TextFormat& fmt);
    void showItalic(TextFormat& fmt, const char* cp);
};
#endif /* _MsgFmt_ */
