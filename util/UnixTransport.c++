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
#include "FaxClient.h"
#include "UnixTransport.h"
#include "Sys.h"

UnixTransport::UnixTransport(FaxClient& c) : Transport(c) {}
UnixTransport::~UnixTransport() {}

fxBool
UnixTransport::isA(const char* address)
{
     return Sys::isSocketFile(address);
}

#if CONFIG_UNIXTRANSPORT
#include "Socket.h"

extern "C" {
#include <sys/un.h>
}

fxBool
UnixTransport::callServer(fxStr& emsg)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
	emsg = "Can not create socket to connect to server.";
	return (FALSE);
    }
    struct sockaddr_un Sun;
    memset(&Sun, 0, sizeof (Sun));
    Sun.sun_family = AF_UNIX;
    strncpy(Sun.sun_path, client.getHost(), sizeof (Sun.sun_path));
    if (client.getVerbose())
	client.traceServer("connect to server at %s",
	    (const char*) client.getHost());
    if (Socket::connect(fd, &Sun, sizeof (Sun)) >= 0) {
	client.setCtrlFds(fd, dup(fd));
	return (TRUE);
    } else {
	emsg = fxStr::format("Can not reach server at Unix domain socket \"%s\".",
	    (const char*) client.getHost());
	Sys::close(fd), fd = -1;
	return (FALSE);
    }
}

fxBool
UnixTransport::initDataConn(fxStr&)
{
#ifdef notdef
    struct sockaddr_in data_addr;
    Socket::socklen_t dlen = sizeof (data_addr);
    if (Socket::getsockname(fileno(client.getCtrlFd()), &data_addr, &dlen) < 0) {
	emsg = fxStr::format("getsockname(ctrl): %s", strerror(errno));
	return (FALSE);
    }
    data_addr.sin_port = 0;		// let system allocate port
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
	emsg = fxStr::format("socket: %s", strerror(errno));
	return (FALSE);
    }
    if (Socket::bind(fd, &data_addr, sizeof (data_addr)) < 0) {
	emsg = fxStr::format("bind: %s", strerror(errno));
	goto bad;
    }
    dlen = sizeof (data_addr);
    if (Socket::getsockname(fd, &data_addr, &dlen) < 0) {
	emsg = fxStr::format("getsockname: %s", strerror(errno));
	goto bad;
    }
    if (listen(fd, 1) < 0) {
	emsg = fxStr::format("listen: %s", strerror(errno));
	goto bad;
    }
    const char* a = (const char*) &data_addr.sin_addr;
    const char* p = (const char*) &data_addr.sin_port;
#define UC(b) (((int) b) & 0xff)
    if (client.command("PORT %u,%u,%u,%u,%u,%u",
	UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
	UC(p[0]), UC(p[1])) != COMPLETE)
	return (FALSE);
#undef UC
    client.setDataFd(fd);
    return (TRUE);
bad:
    Sys::close(fd), fd = -1;
#endif
    return (FALSE);
}

fxBool
UnixTransport::openDataConn(fxStr&)
{
#ifdef notdef
    int s = Socket::accept(fileno(client.getDataFd()), NULL, NULL);
    if (s >= 0) {
	client.setDataFd(s);
	return (TRUE);
    } else {
	emsg = fxStr::format("accept: %s", strerror(errno));
	return (FALSE);
    }
#else
    return (FALSE);
#endif
}
#else
fxBool UnixTransport::callServer(fxStr& emsg)
    { notConfigured("Unix domain", emsg); return (FALSE); }
fxBool UnixTransport::initDataConn(fxStr& emsg)
    { notConfigured("Unix domain", emsg); return (FALSE); }
fxBool UnixTransport::openDataConn(fxStr& emsg)
    { notConfigured("Unix domain", emsg); return (FALSE); }
#endif
