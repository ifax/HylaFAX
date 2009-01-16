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
#ifndef _Socket_
#define	_Socket_

#include "Types.h"				// includes port.h

extern "C" {
#include <sys/socket.h>
#include <sys/un.h>
#if HAS_NETERRNO_H
#include <net/errno.h>
#endif
#include <netdb.h>
}

/*
 * Wrapper functions for C library socket calls.
 *
 * These exist to isolate system dependencies and to insure that
 * proper type casts are done at the call sites.  Note that the
 * actual number of functions in this class could be a lot larger;
 * only those functions that potentially cause portability problems
 * due to missing implicit casts of function parameters are included
 * here.
 *
 * NB: socklen_t is defined based on a #define that configure writes
 *     to port.h.  If this #define exists, then it should specify the
 *     type of the call-by-reference parameters required by the
 *     operating system (some vendors have changed this from int,
 *     sometimes because of 64-bit reasons but mostly because they
 *     did not properly consider the consequences).
 */

class Socket {
public:
    static int accept(int s, void* addr, socklen_t* addrlen);
    static int bind(int s, const void* addr, socklen_t addrlen);
    static int connect(int s, const void* addr, socklen_t addrlen);
    static int getpeername(int s, void* name, socklen_t* namelen);
    static int getsockname(int s, void* name, socklen_t* namelen);
    static int setsockopt(int s, int level, int oname, const void* oval, socklen_t olen);
    static struct hostent* gethostbyname(const char* name);

    union Address
    {
	    sa_family_t family;
	    struct sockaddr_in in;
	    struct sockaddr_in6 in6;
	    struct sockaddr_un un;
	    operator struct sockaddr_in&() { return in; };
	    operator struct sockaddr_in6&() { return in6; };
	    operator struct sockaddr_un&() { return un; };
    };

    static socklen_t socklen(const Address& a);
    static size_t addrlen(const Address& a);
    static in_port_t& port(Address& a);
    static void* addr (Address& a);
};

inline int Socket::accept(int s, void* addr, socklen_t* addrlen)
{
#ifdef CONFIG_HPUX_SOCKLEN_T_BRAINDAMAGE
    return ::accept(s, (struct sockaddr*) addr, (int*)addrlen);
#else
    return ::accept(s, (struct sockaddr*) addr, addrlen);
#endif
}

inline int Socket::bind(int s, const void* addr, socklen_t addrlen)
{
    return ::bind(s, (const struct sockaddr*) addr, addrlen);
}

inline int Socket::connect(int s, const void* addr, socklen_t addrlen)
{
    return ::connect(s, (const struct sockaddr*) addr, addrlen);
}

inline int Socket::getpeername(int s, void* name, socklen_t* namelen)
{
#ifdef CONFIG_HPUX_SOCKLEN_T_BRAINDAMAGE
    return ::getpeername(s, (struct sockaddr*) name, (int*)namelen);
#else
    return ::getpeername(s, (struct sockaddr*) name, namelen);
#endif
}

inline int Socket::getsockname(int s, void* name, socklen_t* namelen)
{
#ifdef CONFIG_HPUX_SOCKLEN_T_BRAINDAMAGE
    return ::getsockname(s, (struct sockaddr*) name, (int*)namelen);
#else
    return ::getsockname(s, (struct sockaddr*) name, namelen);
#endif
}

inline int Socket::setsockopt(int s, int level, int oname, const void* oval, socklen_t olen)
{
    return ::setsockopt(s, level, oname, (const char*) oval, olen);
}

inline struct hostent* Socket::gethostbyname(const char* name)
{
    return ::gethostbyname(name);
}

inline socklen_t Socket::socklen (const Address& a)
{
    switch (a.family)
    {
	case AF_INET:
		return sizeof(a.in);
	case AF_INET6:
		return sizeof(a.in6);
	case AF_UNIX:
		return sizeof(a.un);
    }
    return sizeof(a);
}

inline size_t Socket::addrlen (const Address& a)
{
    switch (a.family)
    {
	case AF_INET:
		return sizeof(a.in.sin_addr);
	case AF_INET6:
		return sizeof(a.in6.sin6_addr);
    }
    fxAssert(true, "Socket::addrlen on invalid Address");
}
inline in_port_t& Socket::port (Address& a)
{
    switch (a.family)
    {
	case AF_INET:
		return a.in.sin_port;
	case AF_INET6:
		return a.in6.sin6_port;
    }
    fxAssert(true, "Socket::port on invalid Address");
}

inline void* Socket::addr (Address& a)
{
    switch (a.family)
    {
	case AF_INET:
	    return (void*) &a.in.sin_addr;
	case AF_INET6:
	    return (void*) &a.in6.sin6_addr;
	case AF_UNIX:
	    return (void*) &a.un.sun_path;
    }
    return NULL;
}
#endif /* _Socket_ */
