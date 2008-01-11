/*	$Id$ */
/*
 * Copyright (c) 2007 Patrice Fournier
 * Copyright (c) 2007 iFAX Solutions Inc.
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
#ifndef _NLS_
#define	_NLS_

#include "config.h"

extern void setupNLS();

#define _(String) gettext(String)
#define N_(String) gettext_noop(String)
#define gettext_noop(String) String

#ifdef ENABLE_NLS

#include <locale.h>
#include <libintl.h>

#else /* ENABLE_NLS */

#define gettext(String) (String)
#define textdomain(Domain) do {} while()
#define bindtextdomain(Package, Directory) do {} while()
#define ngettext(s1, s2, n) (n==1?s1:s2)

#endif /* ENABLE_NLS */

#endif /* _NLS_ */
