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

#if HAS_FCNTL
/*
 * flock emulation for System V using fcntl
 *
 * flock is just mapped to fcntl 
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#if HAS_NETERRNO_H
#include <net/errno.h>
#endif

#if defined(F_WRLCK) && defined(F_RDLCK) && defined(F_UNLCK)
int
flock(int fd, int operation)
{
    struct flock flock;
    int r;
    
    memset(&flock, '\0', sizeof(flock));
    if (operation & LOCK_EX)
	flock.l_type = F_WRLCK;
    else if (operation & LOCK_SH)
	flock.l_type = F_RDLCK;
    else
	flock.l_type = F_UNLCK;
    flock.l_whence = SEEK_SET;
    
    if (((r=fcntl(fd, (operation & LOCK_NB) ? F_SETLK:F_SETLKW, &flock)) == -1)
		&& (errno == EACCES || errno == EAGAIN))
			errno = EWOULDBLOCK;
    return r;
}

#define	FCNTL_EMULATED	1
#endif /* F_WRLCK && F_RDLCK && F_UNLCK */
#endif /* HAS_FCNTL */

#if HAS_LOCKF && !FCNTL_EMULATED
/*
 * flock (fd, operation)
 *
 * This routine performs some file locking like the BSD 'flock'
 * on the object described by the int file descriptor 'fd',
 * which must already be open.
 *
 * The operations that are available are:
 *
 * LOCK_SH  -  get a shared lock.
 * LOCK_EX  -  get an exclusive lock.
 * LOCK_NB  -  don't block (must be ORed with LOCK_SH or LOCK_EX).
 * LOCK_UN  -  release a lock.
 *
 * Return value: 0 if lock successful, -1 if failed.
 *
 * Note that whether the locks are enforced or advisory is
 * controlled by the presence or absence of the SETGID bit on
 * the executable.
 *
 * Note that there is no difference between shared and exclusive
 * locks, since the 'lockf' system call in SYSV doesn't make any
 * distinction.
 *
 * The file "<sys/file.h>" should be modified to contain the definitions
 * of the available operations, which must be added manually (see below
 * for the values).
 *
 * This comes from a regular post in comp.sys.hp.  /lars-owe
 */

#include <unistd.h>
#include <sys/file.h>
#include <errno.h>

extern int errno;

int
flock(int fd, int operation)
{
    int i;

    switch (operation) {
    case LOCK_SH:		/* get a shared lock */
    case LOCK_EX:		/* get an exclusive lock */
	    i = lockf(fd, F_LOCK, 0);
	    break;
    case LOCK_SH|LOCK_NB:	/* get a non-blocking shared lock */
    case LOCK_EX|LOCK_NB:	/* get a non-blocking exclusive lock */
	i = lockf(fd, F_TLOCK, 0);
	if (i == -1)
	    if ((errno == EAGAIN) || (errno == EACCES))
		errno = EWOULDBLOCK;
	break;
    case LOCK_UN:		/* LOCK_UN - unlock */
	i = lockf(fd, F_ULOCK, 0);
	break;
    default:			/* Default - can't decipher operation */
	i = -1;
	errno = EINVAL;
	break;
    }
    return (i);
}

#define	FCNTL_EMULATED	1
#endif

#if !FCNTL_EMULATED
HELP NO FLOCK EMULATION FOR YOUR SYSTEM
#endif
