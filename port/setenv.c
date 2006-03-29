/*	$Id$
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics, Inc.
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
#include "port.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
setenv(const char* name, const char* value, int overwrite)
{
    char* new_env;
    int len;

    if (! overwrite)
    {
	/*
	 * If we are not to overwrite the previous one,
	 * We have to return if it's found
	 */
	if (getenv(name) )
	    return 0;
    }

    /*
     * The length we need is:
     *  - length of name
     *  - =
     *  - length of value
     *  - \0
     */
    len = strlen(name) + strlen(value) + 2;
    if ( (new_env = malloc(len)) == NULL)
    {
	errno = ENOMEM;
	return -1;
    }
    sprintf(new_env, "%s=%s", name, value);
    return putenv(new_env);
}
