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
#ifndef _MIMEState_
#define	_MIMEState_
/*
 * MIME Decoder Support.
 */
#include "Str.h"

enum Encoding {			// content-transfer-encoding
    ENC_7BIT,			// 7-bit data
    ENC_QPRINT,			// quoted-printable text
    ENC_BASE64,			// base64-encoded data
    ENC_8BIT,			// 8-bit data
    ENC_BINARY,			// ``binary'' data
    ENC_UUENCODE		// uuencode'd data
};

enum Charset {			// charset
    CS_USASCII,			// us-ascii
    CS_ISO8859_1,		// iso-8859-1
    CS_ISO8859_2,		// iso-8859-2
    CS_ISO8859_3,		// iso-8859-3
    CS_ISO8859_4,		// iso-8859-4
    CS_ISO8859_5,		// iso-8859-5
    CS_ISO8859_6,		// iso-8859-6
    CS_ISO8859_7,		// iso-8859-7
    CS_ISO8859_8,		// iso-8859-8
    CS_ISO8859_9		// iso-8859-9
};

class fxStackBuffer;
class MsgFmt;

class MIMEState {
private:
    MIMEState*	parent;
    fxStr	type;			// content type
    fxStr	subtype;		// content subtype
    fxStr	desc;			// content description
    fxStr	cid;			// content ID
    fxStr	boundary;		// multipart boundary marker
    u_int	blen;			// adjusted boundary length
    fxBool	lastPart;		// TRUE if last multipart boundary seen
    Encoding	encode;			// content transfer encoding
    Charset	charset;		// text character set

    static fxBool parseToken(const char*&, const char delimeter, fxStr& result);

    void parseParameters(const char*);
protected:
    virtual fxBool setParameter(const fxStr& param, const fxStr& value);

    fxBool getQuotedPrintableLine(FILE* fd, fxStackBuffer& buf);
    fxBool getBase64Line(FILE* fd, fxStackBuffer& buf);
    fxBool getUUDecodeLine(FILE* fd, fxStackBuffer& buf);
public:
    MIMEState(const char* type, const char* subtype);
    MIMEState(MIMEState& parent);
    MIMEState(MIMEState& parent, const char* type, const char* subtype);
    ~MIMEState();

    u_int	lineno;			// input line number
    fxBool parse(const MsgFmt&, fxStr& emsg);

    const fxStr& getType(void) const;
    const fxStr& getSubType(void) const;
    fxBool isParent(const char* type) const;
    fxBool isParent(const char* type, const char* subtype) const;
    const fxStr& getDescription(void) const;
    const fxStr& getContentID(void) const;

    virtual void setEncoding(const char*);
    Encoding getEncoding(void) const;
    virtual void setCharset(const char*);
    Charset getCharset(void) const;
    void setBoundary(const char*);
    const fxStr& getBoundary(void) const;

    virtual fxBool getLine(FILE*, fxStackBuffer&);
    fxBool isLastPart(void) const;

    virtual void trace(FILE*);
};
inline Charset MIMEState::getCharset(void) const	{ return charset; }
inline Encoding MIMEState::getEncoding(void) const	{ return encode; }
inline const fxStr& MIMEState::getBoundary(void) const	{ return boundary; }
inline fxBool MIMEState::isLastPart(void) const		{ return lastPart; }
inline fxBool MIMEState::isParent(const char* t) const
    { return (parent && parent->type == t); }
inline fxBool MIMEState::isParent(const char* t, const char* st) const
    { return (parent && parent->type == t && parent->subtype == st); }
inline const fxStr& MIMEState::getType(void) const	{ return type; }
inline const fxStr& MIMEState::getSubType(void) const	{ return subtype; }
inline const fxStr& MIMEState::getDescription(void) const { return desc; }
inline const fxStr& MIMEState::getContentID(void) const	{ return cid; }
#endif /* _MIMEState_ */
