/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#include "config.h"
#include "Transport.h"
#include "FaxClient.h"
#include "InetTransport.h"
#include "UnixTransport.h"
#include "Sys.h"

#include <errno.h>

Transport::Transport(FaxClient& c) : client(c) {}
Transport::~Transport() {}
bool Transport::hangupServer()	{ return (true); }

void
Transport::closeDataConn(int fd)
{
    (void) Sys::close(fd);
}

Transport&
Transport::getTransport(FaxClient& client, const char* address)
{
    if (address[0] == '\0') {
	/*
	 * An unqualified destination; look for
	 * the best available transport facility.
	 */
	if (UnixTransport::isA(FAX_DEFUNIX)) {
	    client.setHost(FAX_DEFUNIX);
	    return *new UnixTransport(client);
	} else {
	    client.setHost(FAX_DEFHOST);
	    return *new InetTransport(client);
	}
    } else {
	if (UnixTransport::isA(address))
	    return *new UnixTransport(client);
	else
	    return *new InetTransport(client);
    }
}

bool
Transport::abortCmd(fxStr& emsg)
{
    static const char msg[] = { 'A', 'B', 'O', 'R', '\r', '\n' };
    int s = fileno(client.getCtrlFd());
    if (Sys::write(s, msg, sizeof (msg)) != sizeof (msg)) {
	emsg = fxStr::format("send(ABOR\\r\\n): %s", strerror(errno));
	return (false);
    }
    return (true);
}

void
Transport::notConfigured(const char* what, fxStr& emsg)
{
    emsg = fxStr::format("Sorry, no %s communication support was configured.", what);
}
