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
#include "NLS.h"


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


/*
 *  References for IPv6:
 *   http://people.redhat.com/drepper/userapi-ipv6.html
 *   http://www.ietf.org/rfc/rfc2428.txt
 */

bool
InetTransport::callServer(fxStr& emsg)
{

    fxStr service(FAX_SERVICE);
    fxStr proto(client.getProtoName());

    int protocol = 0;
    struct addrinfo hints, *ai;

    if (client.getPort() != -1)
	service = fxStr::format("%d", client.getPort());
    else {
	char* cp;
	if ((cp = getenv("FAXSERVICE")) && *cp != '\0') {
	    fxStr s(cp);
	    u_int l = s.next(0,'/');
	    service = s.head(l);
	    if (l < s.length())
		proto = s.tail(s.length()-(l+1));
	}
    }

    const char* cproto = proto;			// XXX for busted include files
    struct protoent* pp = getprotobyname(cproto);
    if (!pp) {
	client.printWarning(NLS::TEXT("%s: No protocol definition, using default."),
	    cproto);
    } else
	protocol = pp->p_proto;

    memset (&hints, '\0', sizeof (hints));
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG|AI_CANONNAME;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = protocol;

    int err = getaddrinfo (client.getHost(), service, &hints, &ai);
    if (err == EAI_NONAME) {
	hints.ai_flags &= ~AI_NUMERICHOST;
	err = getaddrinfo (client.getHost(), service, &hints, &ai);
    }
    if (err != 0) {
	client.printWarning(NLS::TEXT("getaddrinfo failed with %d: %s"), err, gai_strerror(err));
	return false;
    }

    for (struct addrinfo *aip = ai; aip != NULL; aip = aip->ai_next)
    {
	Socket::Address *addr = (Socket::Address*)aip->ai_addr;
	char buf[256];				// For inet_ntop use
	fxAssert(aip->ai_family == addr->family, "addrinfo ai_family doesn't match in_addr->ai_info");
	if (client.getVerbose())
	    client.traceServer(NLS::TEXT("Trying %s [%d] (%s) at port %u..."),
		    (const char*)client.getHost(), addr->family,
		    inet_ntop(addr->family, Socket::addr(*addr), buf, sizeof(buf)),
		    ntohs(Socket::port(*addr)));
	int fd = socket (aip->ai_family, aip->ai_socktype, aip->ai_protocol);
	if (fd != -1 && ( connect(fd, aip->ai_addr, aip->ai_addrlen) == 0))
	{
	    if (client.getVerbose())
		client.traceServer(NLS::TEXT("Connected to %s."), aip->ai_canonname);
	    /*  Free the addrinfo chain before we leave*/
	    freeaddrinfo(ai);
#if defined(IP_TOS) && defined(IPTOS_LOWDELAY)
	    int tos = IPTOS_LOWDELAY;
	    if (Socket::setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
		client.printWarning(NLS::TEXT("setsockopt(TOS): %s (ignored)"),
		    strerror(errno));
#endif
#ifdef SO_OOBINLINE
	    int on = 1;
	    if (Socket::setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &on, sizeof (on)) < 0)
		client.printWarning(NLS::TEXT("setsockopt(OOBLINE): %s (ignored)"),
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
	Sys::close(fd), fd = -1;
    }

    emsg = fxStr::format(NLS::TEXT("Can not reach service %s at host \"%s\"."),
	(const char*)service, (const char*) client.getHost());
    freeaddrinfo(ai);
    return (false);
}

bool
InetTransport::initDataConn(fxStr& emsg)
{
    struct sockaddr data_addr;
    socklen_t dlen = sizeof (data_addr);

    if (Socket::getsockname(fileno(client.getCtrlFd()), &data_addr, &dlen) < 0) {
	emsg = fxStr::format("getsockname(ctrl): %s", strerror(errno));
	return (false);
    }

    return initDataConnV6(emsg);
}

bool
InetTransport::initDataConnV4(fxStr& emsg)
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
InetTransport::initDataConnV6(fxStr& emsg)
{
    /*
     *  IPv6 extensions to FTP
     *    http://www.ietf.org/rfc/rfc2428.txt
     */
    char buf[1024];
    Socket::Address data_addr;
    socklen_t dlen = sizeof(data_addr);


    if (client.isPassive()) {
	if (Socket::getpeername(fileno(client.getCtrlFd()), &data_addr, &dlen) < 0) {
	    emsg = fxStr::format("getsockname(ctrl): %s", strerror(errno));
	    return (false);
	}
	int err = client.command("EPSV");
	if (err == FaxClient::COMPLETE) {
	    u_int s = client.getLastResponse().next(0, '(');
	    u_int e = client.getLastResponse().next(s, ')');
	    if (s++ < e && e < client.getLastResponse().length()) {
		fxStr p = client.getLastResponse().extract(s, e-s);
		char c = p[0];
		if (p[0] == c && p[1] == c && p[2] == c && p[p.length()-1] == c)
		    Socket::port(data_addr) = htons(p.extract(3, p.length()-4));
		else {
		    client.printWarning(NLS::TEXT("Couldn't parse last response \"%s\""), (const char*)client.getLastResponse());
		    return(false);
		}
	    } else {
		client.printWarning(NLS::TEXT("Couldn't parse last response \"%s\""), (const char*)client.getLastResponse());
		return(false);
	    }
	} else if (err == FaxClient::ERROR && data_addr.family == AF_INET) {

	    client.printWarning(NLS::TEXT("EPSV not supported, trying PASV since we're AF_INET\n"));
	    if (client.command("PASV") != FaxClient::COMPLETE)
		return false;
	    const char *cp = strchr(client.getLastResponse(), '(');
	    if (!cp) {
		client.printWarning(NLS::TEXT("Couldn't parse last response \"%s\""), (const char*)client.getLastResponse());
		return (false);
	    }
	    cp++;
	    unsigned int v[6];
	    int n = sscanf(cp, "%u,%u,%u,%u,%u,%u", &v[2],&v[3],&v[4],&v[5],&v[0],&v[1]);
	    if (n != 6) return (false);
	    if (!inet_aton(fxStr::format("%u.%u.%u.%u", v[2],v[3],v[4],v[5]), &data_addr.in.sin_addr)) {
		return (false);
	    }
	    data_addr.in.sin_port = htons((v[0]<<8)+v[1]);
	    data_addr.in.sin_family = AF_INET;
	    dlen = sizeof(data_addr.in);
	}
    } else {
	if (Socket::getsockname(fileno(client.getCtrlFd()), &data_addr, &dlen) < 0) {
	    emsg = fxStr::format("getsockname(ctrl): %s", strerror(errno));
	    return (false);
	}
	Socket::port(data_addr) = 0;		// let system allocate port
    }

    int fd = socket(data_addr.family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
	emsg = fxStr::format("socket: %s", strerror(errno));
	return (false);
    }
    if (client.isPassive()) {
	if (Socket::connect(fd, &data_addr.in, Socket::socklen(data_addr)) >= 0) {
	    if (client.getVerbose())
		client.traceServer("Connected to %s at port %u.",
		inet_ntop(data_addr.family, Socket::addr(data_addr), buf, sizeof(buf)), ntohs(Socket::port(data_addr)));
	} else {
	    emsg = fxStr::format("Can not reach server at %s at port %u (%s).",
		inet_ntop(data_addr.family, Socket::addr(data_addr), buf, sizeof(buf)), ntohs(Socket::port(data_addr)), strerror(errno));
	    goto bad;
	}
    } else {
	if (Socket::bind(fd, &data_addr, dlen) < 0) {
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

	char hostbuf[128];
	char portbuf[64];

	getnameinfo((struct sockaddr*)&data_addr, dlen,
			hostbuf, sizeof(hostbuf), portbuf, sizeof(portbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
	int err = client.command("EPRT |%d|%s|%s|",
			(data_addr.family == AF_INET6 ? 2 : 1),
			hostbuf, portbuf);
	if (err == FaxClient::ERROR && data_addr.family == AF_INET)
	{
	    if (client.getVerbose())
		    client.printWarning(NLS::TEXT("EPRT not supported, trying PORT"));
	    const char* a; a = (const char*)&data_addr.in.sin_addr;	// XXX for __GNUC__
	    const char* p; p = (const char*)&data_addr.in.sin_port;	// XXX for __GNUC__
#define UC(b) (((int) b) & 0xff)
	    err = client.command("PORT %u,%u,%u,%u,%u,%u",
				UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
				UC(p[0]), UC(p[1]));
#undef UC
	}
	if (err != FaxClient::COMPLETE)
	    return false;
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
    emsg = NLS::TEXT("Sorry, no TCP/IP communication support was configured.");
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
