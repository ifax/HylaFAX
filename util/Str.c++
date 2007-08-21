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
#include "Str.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>

#define DEFAULT_FORMAT_BUFFER 4096

char fxStr::emptyString = '\0';
fxStr fxStr::null;

fxStr::fxStr(u_int l)
{
    slength = l+1;
    if (l>0) {
	data = (char*) malloc(slength);
	memset(data,0,slength);
    } else
	data = &emptyString;
}

fxStr::fxStr(const char *s)
{
    u_int l = strlen(s)+1;
    if (l>1) {
	data = (char*) malloc(l);
	memcpy(data,s,l);
    } else {
	data = &emptyString;
    }
    slength = l;
}

fxStr::fxStr(const char *s, u_int len)
{
    if (len>0) {
	data = (char*) malloc(len+1);
	memcpy(data,s,len);
	data[len] = 0;
    } else
	data = &emptyString;
    slength = len+1;
}

fxStr::fxStr(const fxStr& s)
{
    slength = s.slength;
    if (slength > 1) {
	data = (char*) malloc(slength);
	memcpy(data,s.data,slength);
    } else {
	data = &emptyString;
    }
}

fxStr::fxStr(const fxTempStr& t)
{
    slength = t.slength;
    if (t.slength>1) {
	data = (char*) malloc(slength);
	memcpy(data,t.data,slength);
    } else {
	data = &emptyString;
    }
}

fxStr::fxStr(int a, const char * format)
{
    fxStr s = fxStr::format((format) ? format : "%d", a);
    slength = s.slength;
    if (slength > 1) {
        data = (char*) malloc(slength);
        memcpy(data, s.data, slength);
    } else {
        data = &emptyString;
    }
}

fxStr::fxStr(long a, const char * format)
{
    fxStr s = fxStr::format((format) ? format : "%ld", a);
    slength = s.slength;
    if (slength > 1) {
        data = (char*) malloc(slength);
        memcpy(data, s.data, slength);
    } else {
        data = &emptyString;
    }
}

fxStr::fxStr(float a, const char * format)
{
    fxStr s = fxStr::format((format) ? format : "%g", a);
    slength = s.slength;
    if (slength > 1) {
        data = (char*) malloc(slength);
        memcpy(data, s.data, slength);
    } else {
        data = &emptyString;
    }
}

fxStr::fxStr(double a, const char * format)
{
    fxStr s = fxStr::format((format) ? format : "%lg", a);
    slength = s.slength;
    if (slength > 1) {
        data = (char*) malloc(slength);
        memcpy(data, s.data, slength);
    } else {
        data = &emptyString;
    }
}

fxStr::~fxStr()
{
    assert(data);
    if (data != &emptyString) free(data);
}

fxStr
fxStr::format(const char* fmt ...)
{
    int size = DEFAULT_FORMAT_BUFFER;
    fxStr s;
    va_list ap;
    va_start(ap, fmt);
    s.data = (char*)malloc(size);
    int len = vsnprintf(s.data, size, fmt, ap);
    va_end(ap);
    while (len < 0 || len >= size) {
	if (len < 0 && errno != 0)
	    return s;
	if (len >= size) {
            size = len + 1;
        } else {
            size *= 2;
        }
        s.data = (char*)realloc(s.data, size);
        va_start(ap, fmt);
        len = vsnprintf(s.data, size, fmt, ap);
        va_end(ap);
    }
    if (size > len + 1) {
        s.data = (char*) realloc(s.data, len + 1);
    }
    s.slength = len + 1;
    return s; //XXX this is return by value which is inefficient
}

fxStr
fxStr::vformat(const char* fmt, va_list ap)
{
    //XXX can truncate but cant do much about it without va_copy
    int size = DEFAULT_FORMAT_BUFFER;
    fxStr s;
    char* tmp = NULL;

    int len = 0;

    do
    {
	if (len)
	    size *= 2;
	tmp = (char*)realloc(tmp, size);
	len = vsnprintf(tmp, size, fmt, ap);
	fxAssert(len >= 0, "Str::vformat() error in vsnprintf");
    } while (len > size);

    if (size > len + 1) {
        tmp = (char*) realloc(tmp, len + 1);
    }

    s.data = tmp;
    s.slength = len + 1;
    return s; //XXX this is return by value which is inefficient
}

fxStr fxStr::extract(u_int start, u_int chars) const
{
    fxAssert(start+chars<slength, "Str::extract: Invalid range");
    return fxStr(data+start,chars);
}

fxStr fxStr::head(u_int chars) const
{
    fxAssert(chars<slength, "Str::head: Invalid size");
    return fxStr(data,chars);
}

fxStr fxStr::tail(u_int chars) const
{
    fxAssert(chars<slength, "Str::tail: Invalid size");
    return fxStr(data+slength-chars-1,chars);
}

void fxStr::lowercase(u_int posn, u_int chars)
{
    if (!chars) chars = slength-1-posn;
    fxAssert(posn+chars<slength, "Str::lowercase: Invalid range");
    while (chars--) {
#if defined(hpux) || defined(__hpux) || defined(__hpux__)
	/*
	 * HPUX (10.x at least) is seriously busted.  According
	 * to the manual page, when compiling in ANSI C mode tolower
	 * is defined as a macro that expands to a function that
	 * is undefined.  It is necessary to #undef tolower before
	 * using it! (sigh)
	 */
#ifdef tolower
#undef tolower
#endif
	data[posn] = tolower(data[posn]);
#elif defined(_tolower)
	char c = data[posn];
	if (isupper(c))
	    data[posn] = _tolower(c);
#else
	data[posn] = tolower(data[posn]);
#endif
	posn++;
    }
}

void fxStr::raisecase(u_int posn, u_int chars)
{
    if (!chars) chars = slength-1-posn;
    fxAssert(posn+chars<slength, "Str::raisecase: Invalid range");
    while (chars--) {
#ifdef hpux				// HPUX bogosity; see above
#ifdef toupper
#undef toupper
#endif
	data[posn] = toupper(data[posn]);
#elif defined(_toupper)
	char c = data[posn];
	if (islower(c))
	    data[posn] = _toupper(c);
#else
	data[posn] = toupper(data[posn]);
#endif
	posn++;
    }
}

/*
 * Although T.32 6.1.1 and T.31 6.1 may lead a DCE to not
 * distinguish between lower case and upper case, many DCEs
 * actually support lower case characters in quoted strings.
 * Thus, we don't rasecase quoted strings.
 */
void fxStr::raiseatcmd(u_int posn, u_int chars)
{
    if (!chars) chars = slength-1-posn;
    fxAssert(posn+chars<slength, "Str::raiseatcmd: Invalid range");
    bool quoted = false;
    while (chars--) {
#ifdef hpux				// HPUX bogosity; see above
#ifdef toupper
#undef toupper
#endif
	if (!quoted)
	    data[posn] = toupper(data[posn]);
#elif defined(_toupper)
	char c = data[posn];
	if (islower(c) && !quoted)
	    data[posn] = _toupper(c);
#else
	if (!quoted)
	    data[posn] = toupper(data[posn]);
#endif
	if (data[posn] == '\"')
	    quoted = !quoted;
	posn++;
    }
}

fxStr fxStr::copy() const
{
    return fxStr(data,slength-1);
}

void fxStr::remove(u_int start, u_int chars)
{
    fxAssert(start+chars<slength,"Str::remove: Invalid range");
    long move = slength-start-chars;		// we always move at least 1
    assert(move > 0);
    if (slength - chars <= 1) {
	resizeInternal(0);
	slength = 1;
    } else {
	memmove(data+start, data+start+chars, (u_int)move);
	slength -= chars;
    }
}

fxStr fxStr::cut(u_int start, u_int chars)
{
    fxAssert(start+chars<slength,"Str::cut: Invalid range");
    fxStr a(data+start, chars);
    remove(start, chars);
    return a;
}

void fxStr::insert(const char * v, u_int posn, u_int len)
{
    if (!len) len = strlen(v);
    if (!len) return;
    fxAssert(posn<slength, "Str::insert: Invalid index");
    u_int move = slength - posn;
    u_int nl = slength + len;
    resizeInternal(nl);
    /*
     * When move is one we are always moving \0; but beware
     * that the previous string might have been null before
     * the call to resizeInternal; so set the byte explicitly.
     */
    if (move == 1)
	data[posn+len] = '\0';
    else
	memmove(data+posn+len, data+posn, move);
    memcpy(data+posn, v, len);
    slength = nl;
}

void fxStr::insert(char a, u_int posn)
{
    u_int nl = slength + 1;
    resizeInternal(nl);
    long move = (long)slength - (long)posn;
    fxAssert(move>0, "Str::insert(char): Invalid index");
    /*
     * When move is one we are always moving \0; but beware
     * that the previous string might have been null before
     * the call to resizeInternal; so set the byte explicitly.
     */
    if (move == 1)
	data[posn+1] = '\0';
    else
	memmove(data+posn+1, data+posn, (size_t) move);	// move string tail
    data[posn] = a;
    slength = nl;
}

void fxStr::resizeInternal(u_int chars)
{
    if (slength > 1) {
        if (chars > 0) {
            if (chars >= slength) {
                data = (char*) realloc(data,chars+1);
            }
        } else {
            assert(data != &emptyString);
            free(data);
            data = &emptyString;
        }
    } else {
        assert(data == &emptyString);
        if (chars) {
            data = (char*) malloc(chars+1);
        }
    }
}


void fxStr::resize(u_int chars, bool)
{
    resizeInternal(chars);
    if (chars != 0) {
	if (slength == 1)		// NB: special case for emptyString
	    memset(data, 0, chars+1);
	else {
	    if (chars >= slength)	// zero expanded data segment
		memset(data+slength, 0, chars+1-slength);
	    else			// null terminate shortened string
		data[chars] = 0;
	}
    } else
	;				// now points to emptyString
    slength = chars+1;
}

void fxStr::setMaxLength(u_int len)
{
    if (slength>1) resizeInternal(fxmax(len,slength-1));
}

void fxStr::operator=(const fxTempStr& s)
{
    resizeInternal(s.slength-1);
    memcpy(data,s.data,s.slength);
    slength = s.slength;
}

void fxStr::operator=(const fxStr& s)
{
    if (data == s.data && slength == s.slength)
	return;
    resizeInternal(s.slength-1);
    memcpy(data,s.data,s.slength);
    slength = s.slength;
}

void fxStr::operator=(const char *s)
{
    u_int nl = strlen(s) + 1;
    resizeInternal(nl-1);
    slength = nl;
    memcpy(data,s,slength);
}

void fxStr::append(const char * s, u_int l)
{
    if (!l) l = strlen(s);
    if (!l) return;
    u_int nl = slength + l;
    resizeInternal(nl-1);
    memcpy(data+slength-1, s, l);
    slength = nl;
    data[slength-1] = 0;
}

void fxStr::append(char a)
{
    resizeInternal(slength);
    slength++;
    data[slength-2] = a;
    data[slength-1] = 0;
}

bool operator==(const fxStr& a,const fxStr& b)
{
    return (a.slength == b.slength) && (memcmp(a.data,b.data,a.slength) == 0);
}

bool operator==(const fxStr& a,const char* b)
{
    return (a.slength == strlen(b)+1) && (memcmp(a.data,b,a.slength) == 0);
}

bool operator==(const char* b, const fxStr& a)
{
    return (a.slength == strlen(b)+1) && (memcmp(a.data,b,a.slength) == 0);
} 

bool operator!=(const fxStr& a,const fxStr& b)
{
    return (a.slength != b.slength) || (memcmp(a.data,b.data,a.slength) != 0);
}

bool operator!=(const fxStr& a,const char* b)
{
    return (a.slength != strlen(b)+1) || (memcmp(a.data,b,a.slength) != 0);
}

bool operator!=(const char* b, const fxStr& a)
{
    return (a.slength != strlen(b)+1) || (memcmp(a.data,b,a.slength) != 0);
} 

bool operator>=(const fxStr& a,const fxStr& b)
{
    return strcmp(a,b) >= 0;
}

bool operator>=(const fxStr& a,const char* b)
{
    return strcmp(a,b) >= 0;
}

bool operator>=(const char* a, const fxStr& b)
{
    return strcmp(a,b) >= 0;
} 

bool operator>(const fxStr& a,const fxStr& b)
{
    return strcmp(a,b) > 0;
}

bool operator>(const fxStr& a,const char* b)
{
    return strcmp(a,b) > 0;
}

bool operator>(const char* a, const fxStr& b)
{
    return strcmp(a,b) > 0;
} 

bool operator<=(const fxStr& a,const fxStr& b)
{
    return strcmp(a,b) <= 0;
}

bool operator<=(const fxStr& a,const char* b)
{
    return strcmp(a,b) <= 0;
}

bool operator<=(const char* a, const fxStr& b)
{
    return strcmp(a,b) <= 0;
} 

bool operator<(const fxStr& a,const fxStr& b)
{
    return strcmp(a,b) < 0;
}

bool operator<(const fxStr& a,const char* b)
{
    return strcmp(a,b) < 0;
}

bool operator<(const char* a, const fxStr& b)
{
    return strcmp(a,b) < 0;
} 

int compare(const fxStr&a, const fxStr&b)
{
    return strcmp(a,b);
}

int compare(const fxStr&a, const char*b)
{
    return strcmp(a,b);
}

int compare(const char *a, const char *b)
{
    return strcmp(a,b);
}


static int quickFind(char a, const char * buf, u_int buflen)
{
    while (buflen--)
	if (*buf++ == a) return 1;
    return 0;
}

u_int fxStr::next(u_int posn, char a) const
{
    fxAssert(posn<slength, "Str::next: invalid index");
    char * buf = data+posn;
    u_int counter = slength-1-posn;
    while (counter--) {
	if (*buf == a) return (buf-data);
	buf++;
    }
    return slength-1;
}

u_int fxStr::next(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::next: invalid index");
    char * buf = data + posn;
    u_int counter = slength-1-posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (quickFind(*buf,c,clen)) return (buf-data);
	buf++;
    }
    return slength-1;
}

u_int fxStr::nextR(u_int posn, char a) const
{
    fxAssert(posn<slength, "Str::nextR: invalid index");
    char * buf = data + posn - 1;
    u_int counter = posn;
    while (counter--) {
	if (*buf == a) return (buf-data+1);
	buf--;
    }
    return 0;
}

u_int fxStr::nextR(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::nextR: invalid index");
    char * buf = data + posn - 1;
    u_int counter = posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (quickFind(*buf,c,clen)) return (buf-data+1);
	buf--;
    }
    return 0;
}

u_int fxStr::find(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::find: invalid index");
    char * buf = data + posn;
    u_int counter = slength-1-posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (quickFind(*buf,c,clen) && strncmp(buf,c,clen) == 0)
	    return (buf-data);
	buf++;
    }
    return slength-1;
}

u_int fxStr::findR(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::findR: invalid index");
    char * buf = data + posn - 1;
    u_int counter = posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (quickFind(*buf,c,clen) && strncmp(buf,c,clen) == 0)
	    return (buf-data+1);
	buf--;
    }
    return 0;
}

u_int fxStr::skip(u_int posn, char a) const
{
    fxAssert(posn<slength, "Str::skip: invalid index");
    char * buf = data+posn;
    u_int counter = slength-1-posn;
    while (counter--) {
	if (*buf != a) return (buf-data);
	buf++;
    }
    return slength-1;
}

u_int fxStr::skip(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::skip: invalid index");
    char * buf = data + posn;
    u_int counter = slength-1-posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (!quickFind(*buf,c,clen)) return (buf-data);
	buf++;
    }
    return slength-1;
}

u_int fxStr::skipR(u_int posn, char a) const
{
    fxAssert(posn<slength, "Str::skipR: invalid index");
    char * buf = data + posn - 1;
    u_int counter = posn;
    while (counter--) {
	if (*buf != a) return (buf-data+1);
	buf--;
    }
    return 0;
}

u_int fxStr::skipR(u_int posn, const char * c, u_int clen) const
{
    fxAssert(posn<slength, "Str::skipR: invalid index");
    char * buf = data + posn - 1;
    u_int counter = posn;
    if (!clen) clen = strlen(c);
    while (counter--) {
	if (!quickFind(*buf,c,clen)) return (buf-data+1);
	buf--;
    }
    return 0;
}

fxStr fxStr::token(u_int & posn, const char * delim, u_int dlen) const
{
    fxAssert(posn<slength, "Str::token: invalid index");
    if (!dlen) dlen = strlen(delim);
    u_int end = next(posn, delim, dlen);
    u_int old = posn;
    posn = skip(end, delim, dlen);
    return extract(old,end-old);
}

fxStr fxStr::token(u_int & posn, char a) const
{
    fxAssert(posn<slength, "Str::token: invalid index");
    u_int end = next(posn, a);
    u_int old = posn;
    posn = skip(end, a);
    return extract(old,end-old);
}

fxStr fxStr::tokenR(u_int & posn, const char * delim, u_int dlen) const
{
    fxAssert(posn<slength, "Str::tokenR: invalid index");
    if (!dlen) dlen = strlen(delim);
    u_int begin = nextR(posn, delim, dlen);
    u_int old = posn;
    posn = skipR(begin, delim, dlen);
    return extract(begin, old-begin);
}

fxStr fxStr::tokenR(u_int & posn, char a) const
{
    fxAssert(posn<slength, "Str::tokenR: invalid index");
    u_int begin = nextR(posn, a);
    u_int old = posn;
    posn = skipR(begin, a);
    return extract(begin,old-begin);
}

u_long fxStr::hash() const
{
    char * elementc = data;
    u_int slen = slength - 1;
    u_long k = 0;
    if (slen < 2*sizeof(k)) {
	if (slen <= sizeof(k)) {
	    memcpy((char *)&k + (sizeof(k) - slen), elementc, slen);
	    k<<=3;
	} else {
	    memcpy((char *)&k + (sizeof(k)*2 - slen), elementc, slen-sizeof(k));
	    k<<=3;
	    k ^= *(u_long *)elementc;
	}
    } else {
	k = *(u_long *)(elementc + sizeof(k));
	k<<=3;
	k ^= *(u_long *)elementc;
    }
    return k;
}

//--- concatenation support ----------------------------------

fxTempStr::fxTempStr(const char *d1, u_int l1, const char *d2, u_int l2)
{
    slength = l1 + l2 + 1;
    if (slength <= sizeof(indata)) {
	data = &indata[0];
    } else {
	data = (char*) malloc(slength);
    }
    memcpy(data,d1,l1);
    memcpy(data+l1,d2,l2);
    data[l1+l2] = 0;
}

fxTempStr::fxTempStr(fxTempStr const &other)
{
    slength = other.slength;
    if (slength <= sizeof (indata)) {
	data = &indata[0];
    } else {
	data = (char*) malloc(slength);
    }
    memcpy(data, other.data, slength);
    data[slength] = 0;
}

fxTempStr::~fxTempStr()
{
    if (data != indata) free(data);
}

fxTempStr& operator|(const fxTempStr& ts, const fxStr &b)
{
    return ((fxTempStr &)ts).concat(b.data, b.slength-1);
}

fxTempStr& operator|(const fxTempStr& ts, const char *b)
{
    return ((fxTempStr &)ts).concat(b, strlen(b));
}

fxTempStr& fxTempStr::concat(const char* b, u_int bl)
{
    if (slength <= sizeof(indata)) {
	// Current temporary is in the internal buffer.  See if the
	// concatenation will fit too.
	if (slength + bl > sizeof(indata)) {
	    // Have to malloc.
	    data = (char*) malloc(slength + bl);
	    memcpy(data, indata, slength - 1);
	}
    } else {
	// Temporary is already too large.
	data = (char*) realloc(data, slength + bl);
    }

    // concatenate data
    memcpy(data+slength-1, b, bl);
    slength += bl;
    data[slength-1] = 0;
    return *this;
}

fxTempStr operator|(const fxStr &a, const fxStr &b)
{
    return fxTempStr(a.data, a.slength-1, b.data, b.slength-1);
}

fxTempStr operator|(const fxStr &a, const char *b)
{
    return fxTempStr(a.data, a.slength-1, b, strlen(b));
}

fxTempStr operator|(const char *a, const fxStr &b)
{
    return fxTempStr(a, strlen(a), b.data, b.slength-1);
}
