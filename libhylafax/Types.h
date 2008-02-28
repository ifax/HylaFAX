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
#ifndef _Types_
#define	_Types_

#include "string.h"
#include "assert.h"
#include "stdio.h"
#include "sys/types.h"
#include "port.h"
#include "config.h"

// Needed for the placement new operator
#ifdef HAS_OLD_NEW_H
#include "new.h"
#else
#include "new"
#endif

// Boolean type
#ifdef NEED_BOOL
typedef u_char bool;
#undef true
#define true ((bool)1)
#undef false
#define false ((bool)0)
#endif

// minimum of two numbers
inline int fxmin(int a, int b)		{ return (a < b) ? a : b; }
inline u_long fxmin(u_long a, u_long b)	{ return (a < b) ? a : b; }
inline u_int fxmin(u_int a, u_int b)	{ return (a < b) ? a : b; }

// maximum of two numbers
inline int fxmax(int a, int b)		{ return (a > b) ? a : b; }
inline u_long fxmax(u_long a, u_long b)	{ return (a > b) ? a : b; }
inline u_int fxmax(u_int a, u_int b)	{ return (a > b) ? a : b; }

#define streq(a, b)	(strcmp(a,b) == 0)
#define	strneq(a, b, n)	(strncmp(a,b,n) == 0)

#ifdef NDEBUG
#define fxAssert(EX,MSG)
#else
extern "C" void _fxassert(const char*, const char*, int);
#define fxAssert(EX,MSG) if (EX); else _fxassert(MSG,__FILE__,__LINE__);
#endif

//----------------------------------------------------------------------

// Use this macro at the end of a multi-line macro definition.  This
// helps eliminate the extra back slash problem.
#define __enddef__

// Some macros for namespace hacking.  These macros concatenate their
// arguments.
#ifdef	__ANSI_CPP__
#define fxIDENT(a) a
#define fxCAT(a,b) a##b
#define fxCAT2(a,b) a##b
#define fxCAT3(a,b,c) a##b##c
#define fxCAT4(a,b,c,d) a##b##c##d
#define fxCAT5(a,b,c,d,e) a##b##c##d##e

#define	fxQUOTE(a) #a
#else
#define fxIDENT(a) a
#define fxCAT(a,b) fxIDENT(a)b
#define fxCAT2(a,b) fxIDENT(a)b
#define fxCAT3(a,b,c) fxCAT2(a,b)c
#define fxCAT4(a,b,c,d) fxCAT3(a,b,c)d
#define fxCAT5(a,b,c,d,e) fxCAT4(a,b,c,d)e

#define	fxQUOTE(a) "a"
#endif

//----------------------------------------------------------------------

// Workaround for difficulties with signal handler definitions (yech)

#ifndef fxSIGHANDLER
#define	fxSIGHANDLER
#endif
#ifndef fxSIGVECHANDLER
#define	fxSIGVECHANDLER
#endif
#ifndef fxSIGACTIONHANDLER
#define	fxSIGACTIONHANDLER
#endif

// --------------------------------------------------------------------
//
// Support for NLS

#define _(String) gettext(String)
#define N_(String) gettext_noop(String)
#define gettext_noop(String) String

#ifdef ENABLE_NLS

#include <locale.h>
#include <libintl.h>

#ifdef NEED_NGETTEXT
#define ngettext(s1,s2,n) (n ==1 ? gettext(s1) : gettext(s2))
#endif

#else /* ENABLE_NLS */

#define gettext(String) (String)
#define textdomain(Domain) do {} while()
#define bindtextdomain(Package, Directory) do {} while()
#define ngettext(s1, s2, n) (n==1?s1:s2)

#endif /* ENABLE_NLS */


#endif /* _Types_ */
