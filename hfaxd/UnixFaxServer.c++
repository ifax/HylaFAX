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
/*
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 *
 * This stuff does not work; it is here as a placeholder in
 * case it becomes worthwhile to add UNIX domain socket support.
 *
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 */
#if CONFIG_UNIXTRANSPORT
#include "Dispatcher.h"
#include "UnixFaxServer.h"
#include "Sys.h"
#include "Socket.h"
#include "config.h"

UnixSuperServer::UnixSuperServer(const char* f, int bl)
    : SuperServer("UNIX", bl)
    , fileName(f)
{}
UnixSuperServer::~UnixSuperServer()
{
    if (fileName != "")
	Sys::unlink(fileName);
}

bool
UnixSuperServer::startServer(void)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s >= 0) {
	struct sockaddr_un Sun;
	Sun.sun_family = AF_UNIX;
	strncpy(Sun.sun_path, fileName, sizeof (Sun.sun_path));
	if (Socket::bind(s, &Sun, sizeof (Sun)) >= 0) {
#ifdef HAS_FCHMOD
	    (void) fchmod(s, 0622);
#else
	    (void) Sys::chmod(fileName, 0622);
#endif
	    (void) listen(s, getBacklog());
	    Dispatcher::instance().link(s, Dispatcher::ReadMask, this);
	    return (true);
	}
	Sys::close(s);
	logError("%s HylaFAX: bind (port %s): %m",
	    getKind(), (const char*) fileName);
	fileName = "";				// don't try to unlink file
    } else
	logError("%s HylaFAX: socket: %m", getKind());
    return (false);
}
HylaFAXServer* UnixSuperServer::newChild(void) { return new UnixFaxServer; }

UnixFaxServer::UnixFaxServer()
{
    usedefault = true;
}
UnixFaxServer::~UnixFaxServer() {}

void
UnixFaxServer::initServer(void)
{
    HylaFAXServer::initServer();
    usedefault = true;
}

void
UnixFaxServer::open(void)
{
    remotehost = hostname;		// always on same machine
    remoteaddr = "local-UNIX";		// XXX
    Dispatcher::instance().link(STDIN_FILENO, Dispatcher::ReadMask, this);
    HylaFAXServer::open();
}

void
UnixFaxServer::passiveCmd(void)
{
    perror_reply(425, "Cannot open passive connection "
	"(makes no sense with UNIX domain sockets)", errno);
}

void
UnixFaxServer::netStatus(FILE* fd)
{
    if (data != -1)
        fprintf(fd, "    Client data connection open\r\n");
    else
        fprintf(fd, "    No client data connection\r\n");
}

/*
 * Creat a socket for a data transfer.
 */
FILE*
UnixFaxServer::getDataSocket(const char* mode)
{
    if (data >= 0)
        return (fdopen(data, mode));
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s >= 0) {
	/* anchor socket to avoid multi-homing problems */
	data_source.sun_family = AF_UNIX;
	strcpy(data_source.sun_path, ctrl_addr.sun_path);
	if (bind(s, (struct sockaddr*) &data_source, sizeof (data_source)) >= 0) {
	    return (fdopen(s, mode));
	}
    }
    (void) Sys::close(s);
    return (NULL);
}

bool
UnixFaxServer::dataConnect(void)
{
    return Socket::connect(data, &data_dest,sizeof (data_dest)) >= 0;
}

/*
 * Establish a data connection for a file transfer operation.
 */
FILE*
UnixFaxServer::openDataConn(const char* mode, int& code)
{
    byte_count = 0;
    if (pdata >= 0) {
        struct sockaddr_un from;
        int fromlen = sizeof(from);
        int s = accept(pdata, (struct sockaddr*) &from, &fromlen);
        if (s < 0) {
            reply(425, "Cannot open data connection.");
            (void) Sys::close(pdata);
            pdata = -1;
            return (NULL);
        }
        (void) Sys::close(pdata);
        pdata = s;
	code = 150;
        return (fdopen(pdata, mode));
    }
    if (data >= 0) {
	code = 125;
        usedefault = 1;
        return (fdopen(data, mode));
    }
    if (usedefault)
        data_dest = peer_addr;
    usedefault = 1;
    FILE* file = getDataSocket(mode);
    if (file == NULL) {
        reply(425, "Cannot create data socket (%s): %s.",
	      data_source.sun_path, strerror(errno));
	return (NULL);
    }
    data = fileno(file);
    if (!dataConnect()) {
	perror_reply(425, "Cannot build data connection", errno);
	fclose(file);
	data = -1;
	return (NULL);
    }
    code = 150;
    return (file);
}

bool
UnixFaxServer::hostPort()
{
    fxStr s;
    if (pathname(s)) {
	data_dest.sun_family = AF_UNIX;
	strncpy(data_dest.sun_path, s, sizeof (data_dest.sun_path));
	return (true);
    } else
	return (false);
}

void
UnixFaxServer::portCmd(void)
{
    logcmd(T_PORT, "%s", data_dest.sun_path);
    usedefault = false;
    if (pdata >= 0)
	(void) Sys::close(pdata), pdata = -1;
    reply(200, "PORT command successful.");
}
#endif
