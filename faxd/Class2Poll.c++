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
#include "Class2.h"
#include "t.30.h"

/*
 * Request to poll remote documents.
 */
bool
Class2Modem::requestToPoll(fxStr& emsg)
{
    if (!class2Cmd(splCmd, 1)) {
	emsg = "Unable to request polling operation"
	    " (modem may not support polling)";
	return (false);
    } else
	return (true);
}

/*
 * Startup a polled receive operation.
 */
bool
Class2Modem::pollBegin(const fxStr& cig, const fxStr& sep, const fxStr& pwd, fxStr& emsg)
{
    const char* cmdFailed = "Unable to setup %s (modem command failed)";

    if (!class2Cmd(cigCmd, cig)) {		// set polling ID
	emsg = fxStr::format(cmdFailed, "polling identifer");
	return (false);
    }
    if (sep != "" && paCmd != "" && !class2Cmd(paCmd, sep)) {
	emsg = fxStr::format(cmdFailed, "selective polling address");
	return (false);
    }
    if (pwd != "" && pwCmd != "" && !class2Cmd(pwCmd, pwd)) {
	emsg = fxStr::format(cmdFailed, "polling password");
	return (false);
    }
    return (true);
}
