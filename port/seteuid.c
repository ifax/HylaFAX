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
#include <unistd.h>

#if defined(hpux) || defined(__hpux) || defined(__hpux__)
int
seteuid(uid_t uid)
{
    return (setresuid(-1, uid, -1));
}
#endif
#ifdef sco
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#define NULL 0

extern errno;

int setreuid(ruid, euid)
int ruid, euid;
{
	int myfile; 
	int res;
	int tmperrno;
	struct socksysreq request;

	if ((myfile = open("/dev/socksys", O_RDWR)) < NULL)
		return(-1);

	request.args[0] = SO_SETREUID;
	request.args[1] = ruid;
	request.args[2] = euid;
	res = ioctl(myfile, SIOCSOCKSYS, &request);
	tmperrno = errno;
	close(myfile);
	errno = tmperrno;
	return(res);
}

int seteuid(uid)
int uid;
{
	return(setreuid(-1, uid));
}
#endif
