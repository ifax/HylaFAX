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

/*
 * Regular expression support.
 */
#include <RE.h>

RE::RE(const char* pat, int len, int flags)
    : _pattern(pat, len == 0 ? strlen(pat) : len)
{
    init(flags);
}
RE::RE(const fxStr& pat, int flags) : _pattern(pat)
{
    init(flags);
}
RE::RE(const RE& other, int flags)
    : fxObj(other)
    , _pattern(other._pattern)
{
    init(flags);
}
RE::~RE()
{
    regfree(&c_pattern);
    delete [] matches;
}

void
RE::init(int flags)
{
    compResult = regcomp(&c_pattern, _pattern, flags);
    if (compResult == 0) {
	matches = new regmatch_t[c_pattern.re_nsub+1];
	execResult = REG_NOMATCH;
    } else {
	matches = NULL;
	execResult = compResult;
    }
}

bool
RE::Find(const char* text, u_int length, u_int off)
{
    if (compResult == 0) {
	/*
	 * These two checks are for compatibility with the old
	 * InterViews code; yech (but the DialRules logic needs it).
	 */
	if (((off || length) && off >= length) || (off && _pattern[0] == '^'))
	    execResult = REG_NOMATCH;
	else {
	    matches[0].rm_so = off;
	    matches[0].rm_eo = length;
	    execResult = regexec(&c_pattern, text, c_pattern.re_nsub+1,
			    matches, REG_STARTEND);
	}
    }
    return (execResult == 0);
}

void
RE::getError(fxStr& emsg) const
{
    char buf[1024];
    (void) regerror(execResult, &c_pattern, buf, sizeof (buf));
    emsg = buf;
}

int
RE::StartOfMatch(u_int ix) const
{
    if (execResult != 0)
	return (execResult);
    return (ix <= c_pattern.re_nsub ? matches[ix].rm_so : -1);
}

int
RE::EndOfMatch(u_int ix) const
{
    if (execResult != 0)
	return (execResult);
    return (ix <= c_pattern.re_nsub ? matches[ix].rm_eo : -1);
}

REPtr::~REPtr() { destroy(); }
void REPtr::destroy() { if (p) p->dec(); }

REPtr&
REPtr::operator=(const REPtr& other)
{
    if (p != other.p) {
	destroy();
	p = other.p ? (other.p->inc(),other.p) : 0;
    }
    return *this;
}

REPtr&
REPtr::operator=(RE* tp)
{
    if (p != tp) {
	destroy();
	p = tp ? (tp->inc(),tp) : 0;
    }
    return *this;
}
