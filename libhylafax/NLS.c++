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
#include "NLS.h"
#include "Types.h"
#include "Sys.h"

#include <errno.h>

void do_bind (const char* domain, const char* ldir)
{
    /* bindtextdomain() doesn't preserver errno */
    int save_errno = errno;

    if (!ldir)
	ldir = getenv("HFLOCALEDIR");
    if (!ldir)
	ldir = LOCALEDIR;
    bindtextdomain(domain, ldir);
    errno = save_errno;
}

const char* NLS::TEXT (const char* msgid)
{
    if (! bound)
    {
	bound = true;
	do_bind(domain, NULL);
    }
    return dgettext(domain, msgid);
}

void NLS::Setup (const char* d, const char* ldir)
{
    setlocale(LC_CTYPE, "");
    setlocale(LC_MESSAGES, "");

    /*
     * We'll take the hit of setting up libhylafax NLS here
     */
    do_bind(domain, NULL);
    bound = true;

    do_bind(d, ldir);
    textdomain(d);
}

const char* NLS::domain = "libhylafax";
bool NLS::bound = false;
