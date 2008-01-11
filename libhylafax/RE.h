/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#ifndef _RE_
#define	_RE_

#include <sys/types.h>
#include "regex.h"
#include "Str.h"
#include "Ptr.h"

/*
 * Reference-counted regular expressions;
 * for use with Ptrs, Arrays, and Dicts.
 */
class RE : public fxObj {
public:
    RE(const char* pat, int length = 0 , int flags = REG_EXTENDED);
    RE(const fxStr& pat, int flags = REG_EXTENDED);
    RE(const RE& other, int flags = REG_EXTENDED);
    ~RE();

    const char* pattern() const;

    bool Find(const char* text, u_int length, u_int off = 0);
    bool Find(const fxStr& s, u_int off = 0);
    int StartOfMatch(u_int subexp = 0) const;
    int EndOfMatch(u_int subexp = 0) const;

    int getErrorCode() const;
    void getError(fxStr&) const;
private:
    int		compResult;		// regcomp result
    int		execResult;		// last regexec result
    fxStr	_pattern;		// original regex
    regex_t	c_pattern;		// compiled regex
    regmatch_t*	matches;		// subexpression matches

    void	init(int flags);
};

inline bool RE::Find(const fxStr& s, u_int off)
    { return Find(s, s.length(), off); }
inline const char* RE::pattern() const	{ return _pattern; }
inline int RE::getErrorCode() const		{ return execResult; }

/*
 * This private REPtr definition is done to work
 * around problems with certain C++ compilers not
 * properly making the destructor method either inline
 * or static.  We also can save some space by eliminating
 * some inline functions that compilers frequently can't
 * handle in-line.
 */
class REPtr {
protected:
    void destroy();
    RE* p;
public:
    REPtr() { p = 0; }
    REPtr(RE *tp) { p = tp ? (tp->inc(),tp) : 0; }
    REPtr(const REPtr& other)
	{ p = other.p ? (other.p->inc(),other.p) : 0; }
    ~REPtr();
    REPtr& operator=(const REPtr& other);
    REPtr& operator=(RE* tp);
    int compare(const REPtr *other) const
	{ return int((char*) p - (char*) other->p); }
    operator RE*() { return p; }
    operator const RE*() const { return p; }
    RE* operator ->() { return p; }
    const RE* operator ->() const { return p; }
};
#endif /* _RE_ */
