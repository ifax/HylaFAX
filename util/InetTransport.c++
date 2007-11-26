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
#include "InetTransport.h"
#include "Sys.h"

InetTransport::InetTransport(FaxClient& c) : Transport(c) {}
InetTransport::~InetTransport(){}

bool
InetTransport::isA(const char*)
{
     return (true);			// XXX are there checks we can make?
}

#if CONFIG_INETTRANSPORT
#include "Socket.h"

#include <sys/types.h>
extern "C" {
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netdb.h>
}
#include <ctype.h>
#include <errno.h>

bool
InetTransport::callServer(fxStr& emsg)
{
    int port = client.getPort();
    fxStr proto(client.getProtoName());
    char* cp;
    if ((cp = getenv("FAXSERVICE")) && *cp != '\0') {
	fxStr s(cp);
	u_int l = s.next(0,'/');
	port = (int) s.head(l);
	if (l < s.length())
	    proto = s.tail(s.length()-(l+1));
    }

    int protocol;
    const char* cproto = proto;			// XXX for busted include files
    struct protoent* pp = getprotobyname(cproto);
    if (!pp) {
	client.printWarning(_("%s: No protocol definition, using default."),
	    cproto);
	protocol = 0;
    } else
	protocol = pp->p_proto;

    struct hostent* hp = Socket::gethostbyname(client.getHost());
    if (!hp) {
	emsg = client.getHost() | _(": Unknown host");
	return (false);
    }
    
    int fd = socket(hp->h_addrtype, SOCK_STREAM, protocol);
    if (fd < 0) {
	emsg = _("Can not create socket to connect to server.");
	return (false);
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof (sin));
    sin.sin_family = hp->h_addrtype;
    if (port == -1) {
	struct servent* sp = getservbyname(FAX_SERVICE, cproto);
	if (!sp) {
	    if (!isdigit(cproto[0])) {
		client.printWarning(
		    _("No \"%s\" service definition, using default %u/%s."),
		    FAX_SERVICE, FAX_DEFPORT, cproto);
		sin.sin_port = htons(FAX_DEFPORT);
	    } else
		sin.sin_port = atoi(cproto);
	} else
	    sin.sin_port = sp->s_port;
    } else
	sin.sin_port = htons(port);
    for (char** cpp = hp->h_addr_list; *cpp; cpp++) {
	memcpy(&sin.sin_addr, *cpp, hp->h_length);
	if (client.getVerbose())
	    client.traceServer(_("Trying %s (%s) at port %u..."),
		(const char*) client.getHost(),
		inet_ntoa(sin.sin_addr),
		ntohs(sin.sin_port));
	if (Socket::connect(fd, &sin, sizeof (sin)) >= 0) {
	    if (client.getVerbose())
		client.traceServer(_("Connected to %s."), hp->h_name);
#if defined(IP_TOS) && defined(IPTOS_LOWDELAY)
	    int tos = IPTOS_LOWDELAY;
	    if (Socket::setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
		client.printWarning(_("setsockopt(TOS): %s (ignored)"),
		    strerror(errno));
#endif
#ifdef SO_OOBINLINE
	    int on = 1;
	    if (Socket::setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &on, sizeof (on)) < 0)
		client.printWarning(_("setsockopt(OOBLINE): %s (ignored)"),
		    strerror(errno));
#endif
	    /*
	     * NB: We dup the descriptor because some systems
	     * that emulate sockets with TLI incorrectly handle
	     * things if we use the same descriptor for both
	     * input and output (sigh).
	     */
	    client.setCtrlFds(fd, dup(fd));
	    return (true);
	}
    }
    emsg = fxStr::format(_("Can not reach server at host \"%s\", port %u."),
	(const char*) client.getHost(), ntohs(sin.sin_port));
    Sys::close(fd), fd = -1;
    return (false);
}

bool
InetTransport::initDataConn(fxStr& emsg)
{
    struct sockaddr_in data_addr;
    socklen_t dlen = sizeof (data_addr);
    if (client.isPassive()) {
	if (client.command("PASV") != FaxClient::COMPLETE)
	    return (false);
	const char *cp = strchr(client.getLastResponse(), '(');
	if (!cp) return (false);
	cp++;
	unsigned int v[6];
	int n = sscanf(cp, "%u,%u,%u,%u,%u,%u", &v[2],&v[3],&v[4],&v[5],&v[0],&v[1]);
	if (n != 6) return (false);
	if (!inet_aton(fxStr::format("%u.%u.%u.%u", v[2],v[3],v[4],v[5]), &data_addr.sin_addr)) {
	    return (false);
	}
	data_addr.sin_port = htons((v[0]<<8)+v[1]);
	data_addr.sin_family = AF_INET;
    } else {
	if (Socket::getsockname(fileno(client.getCtrlFd()), &data_addr, &dlen) < 0) {
	    emsg = fxStr::format("getsockname(ctrl): %s", strerror(errno));
	    return (false);
	}
	data_addr.sin_port = 0;		// let system allocate port
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
	emsg = fxStr::format("socket: %s", strerror(errno));
	return (false);
    }
    if (client.isPassive()) {
	if (Socket::connect(fd, &data_addr, sizeof (data_addr)) >= 0) {
	    if (client.getVerbose())
		client.traceServer("Connected to %s at port %u.",
		inet_ntoa(data_addr.sin_addr), ntohs(data_addr.sin_port));
	} else {
	    emsg = fxStr::format("Can not reach server at %s at port %u (%s).",
		inet_ntoa(data_addr.sin_addr), ntohs(data_addr.sin_port), strerror(errno));
	    goto bad;
	}
    } else {
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
	const char* a; a = (const char*) &data_addr.sin_addr;	// XXX for __GNUC__
	const char* p; p = (const char*) &data_addr.sin_port;	// XXX for __GNUC__
#define UC(b) (((int) b) & 0xff)
	if (client.command("PORT %u,%u,%u,%u,%u,%u",
	    UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
	    UC(p[0]), UC(p[1])) != FaxClient::COMPLETE)
	    return (false);
#undef UC
    }
    client.setDataFd(fd);
    return (true);
bad:
    Sys::close(fd), fd = -1;
    return (false);
}

bool
InetTransport::openDataConn(fxStr& emsg)
{
    if (client.isPassive()) {
	return (client.getDataFd() > 0);
    }
    int s = Socket::accept(client.getDataFd(), NULL, NULL);
    if (s >= 0) {
	client.setDataFd(s);
#if defined(IP_TOS) && defined(IPTOS_THROUGHPUT)
	int tos = IPTOS_THROUGHPUT;
	if (Socket::setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
	    client.printWarning("setsockopt(IP_TOS): %s", strerror(errno));
#endif
	return (true);
    } else {
	emsg = fxStr::format("accept: %s", strerror(errno));
	return (false);
    }
}

/*
 * Send an abort request to terminate an operation.
 * The initial interrupt is sent as urgent data to
 * cause the server to process the subsequent ABOR
 * command immediately.  We send IAC in urgent mode
 * instead of DM because 4.3BSD place the out-of-band
 * mark in the data stream after the urgent byte
 * rather than before as done by most contemporary
 * TCP implementations.
 */
bool
InetTransport::abortCmd(fxStr& emsg)
{
    static const u_char msg[] =
	{ IAC, IP, IAC, DM, 'A', 'B', 'O', 'R', '\r', '\n' };
    int s = fileno(client.getCtrlFd());
    if (send(s, msg, 3, MSG_OOB) != 3) {
	emsg = fxStr::format("send(MSG_OOB): %s", strerror(errno));
	return (false);
    }
    if (send(s, msg+3, sizeof (msg)-3, 0) != sizeof (msg)-3) {
	emsg = fxStr::format("send(<DM>ABOR\\r\\n): %s", strerror(errno));
	return (false);
    }
    return (true);
}
#else
void
Transport::notConfigured(fxStr& emsg)
{
    emsg = _("Sorry, no TCP/IP communication support was configured.");
}

bool InetTransport::callServer(fxStr& emsg)
    { notConfigured(emsg); return (false); }
bool InetTransport::initDataConn(fxStr& emsg)
    { notConfigured(emsg); return (false); }
bool InetTransport::openDataConn(fxStr& emsg)
    { notConfigured(emsg); return (false); }
bool InetTransport::abortDataConn(fxStr& emsg)
    { notConfigured(emsg); return (false); }
#endif
