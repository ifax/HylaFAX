/*	$Id$ */
/*
 * Copyright (c) 1993-1996 Sam Leffler
 * Copyright (c) 1993-1996 Silicon Graphics, Inc.
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
 * FAX Server BSD Getty Support.
 */
#include "GettyBSD.h"

#include <stddef.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <utmp.h>

#include "Sys.h"
#include "config.h"

/*
 * FAX Server BSD-style Getty&co. Support.
 */

/*
 * BSD subprocess support; used by getty-style processes.
 */

BSDSubProc::BSDSubProc(const char* path, const fxStr& l, const fxStr& s) : Getty(path,l,s)
{
}

BSDSubProc::~BSDSubProc()
{
}

/*
 * ``Open'' the device and setup the initial tty state
 * so that the normal stdio routines can be used.
 */
void
BSDSubProc::setupSession(int modemFd)
{
    int fd;
    /*
     * Close everything down except the modem so
     * that the remote side doesn't get hung up on.
     */
    for (fd = Sys::getOpenMax()-1; fd >= 0; fd--)
	if (fd != modemFd)
	    (void) Sys::close(fd);
    /*
     * Now make the line be the controlling tty
     * and create a new process group/session for
     * the login process that follows.
     */
    fd = Sys::open("tty", 0);		// NB: assumes we're in /dev
    if (fd >= 0) {
	ioctl(fd, TIOCNOTTY, 0);
	Sys::close(fd);
    }
    setsid();
    fd = Sys::open(getLine(), O_RDWR|O_NONBLOCK);
    if (fd != STDIN_FILENO)
	fatal("Can not setup \"%s\" as stdin", getLine());
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) &~ O_NONBLOCK))
	fatal("Can not reset O_NONBLOCK: %m");
    Sys::close(modemFd);		// done with this, pitch it

#ifdef TIOCSCTTY
    if (ioctl(fd, TIOCSCTTY, 0))
	fatal("Cannot set controlling tty: %m");
#endif
#ifdef TIOCGETDFLT
    /*
     * FaxServer::setBaudRate forces CLOCAL on for BSDi.  However
     * getty resets the terminal to the system default, if one exists,
     * prior to execing login, but preserves CLOCAL, if it is on.
     * Thus, if a system default setting exists for the tty, we
     * set the CLOCAL state from it; otherwise, we leave CLOCAL on.
     */
    struct termios term, dflt;
    if (ioctl(fd, TIOCGETDFLT, &dflt) == 0 && (dflt.c_cflag & CLOCAL) == 0) {
	tcgetattr(fd, &term);
	term.c_cflag &= ~CLOCAL;
	tcsetattr(fd, TCSAFLUSH, &term);
    }
#else
    /*
     * Turn off CLOCAL so that SIGHUP is sent on modem disconnect.
     */
    struct termios term;
    if (tcgetattr(fd, &term) == 0) {
	term.c_cflag &= ~CLOCAL;
	tcsetattr(fd, TCSAFLUSH, &term);
    }
#endif
    /*
     * Setup descriptors for stdout, and stderr.
     * Establish the initial line termio settings and set
     * protection on the device file.
     */
    Getty::setupSession(fd);
}

/*
 * BSD getty/login-specific subprocess support.
 */

BSDGetty::BSDGetty(const char* path, const fxStr& l, const fxStr& s) : BSDSubProc(path,l,s)
{
}

BSDGetty::~BSDGetty()
{
}

#define	lineEQ(a,b)	(strncmp(a,b,sizeof(a)) == 0)

void
BSDGetty::writeWtmp(utmp* ut)
{
#if HAS_LOGWTMP
    logwtmp(ut->ut_line, "", "");
#else
    int wfd = Sys::open(_PATH_WTMP, O_WRONLY|O_APPEND);
    if (wfd >= 0) {
	struct stat buf;
	if (Sys::fstat(wfd, buf) == 0) {
	    memset(ut->ut_name, 0, sizeof (ut->ut_name));
	    memset(ut->ut_host, 0, sizeof (ut->ut_host));
	    ut->ut_time = Sys::now();
	    if (Sys::write(wfd, (char *)ut, sizeof (*ut)) != sizeof (*ut))
		ftruncate(wfd, buf.st_size);
	}
	Sys::close(wfd);
    }
#endif
}

void
BSDGetty::logout(const char* line)
{
#if HAS_LOGOUT
    ::logout(line);
#else
    int ufd = Sys::open(_PATH_UTMP, O_RDWR);
    if (ufd >= 0) {
	struct utmp ut;
	while (Sys::read(ufd, (char *)&ut, sizeof (ut)) == sizeof (ut))
	    if (ut.ut_name[0] && lineEQ(ut.ut_line, line)) {
		memset(ut.ut_name, 0, sizeof (ut.ut_name));
		memset(ut.ut_host, 0, sizeof (ut.ut_host));
		ut.ut_time = time(0);
		lseek(ufd, -(long) sizeof (ut), SEEK_CUR);
		Sys::write(ufd, (char *)&ut, sizeof (ut));
	    }
	Sys::close(ufd);
    }
#endif
}

void
BSDGetty::hangup()
{
    // at this point we're root and we can reset state
    int ufd = Sys::open(_PATH_UTMP, O_RDONLY);
    if (ufd >= 0) {
	struct utmp ut;
	while (Sys::read(ufd, (char *)&ut, sizeof (ut)) == sizeof (ut))
	    if (ut.ut_name[0] && lineEQ(ut.ut_line, getLine())) {
		writeWtmp(&ut);
		break;
	    }
	Sys::close(ufd);
    }
    logout(getLine());
    Getty::hangup();
}

/*
 * Public Interfaces.
 */
Getty*
OSnewGetty(const fxStr& dev, const fxStr& speed)
{
    return (new BSDGetty(_PATH_GETTY, dev, speed));
}

Getty*
OSnewVGetty(const fxStr& dev, const fxStr& speed)
{
    return (new BSDSubProc(_PATH_VGETTY, dev, speed));
}

Getty*
OSnewEGetty(const fxStr& dev, const fxStr& speed)
{
    return (new BSDSubProc(_PATH_EGETTY, dev, speed));
}
