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
#include "config.h"

#include <limits.h>
#include <stddef.h>
#include <termios.h>
#include <sys/ioctl.h>
extern "C" {
#include <utmp.h>
#if HAS_UTMPX
#if !HAS_EXIT_STATUS
/* workaround SVR4.0.3 header braindamage */
struct exit_status {
	short e_termination;    /* Process termination status */
	short e_exit;           /* Process exit status */
};
#endif /* !HAS_EXIT_STATUS */
#include <utmpx.h>

#define utmp          utmpx
#undef  ut_time
#define ut_time       ut_xtime

#define getutent      getutxent
#define getutid       getutxid
#define getutline     getutxline
#define pututline     pututxline
#define setutent      setutxent
#define endutent      endutxent
#endif	/* HAS_UTMPX */
}

#include "Sys.h"
#include "GettySysV.h"

/*
 * FAX Server System V Getty&co. Support.
 */

/*
 * System V subprocess support; used by getty-style processes.
 */

SysVSubProc::SysVSubProc(const char* path, const fxStr& l, const fxStr& s) : Getty(path,l,s)
{
}

SysVSubProc::~SysVSubProc()
{
}

/*
 * ``Open'' the device and setup the initial tty state
 * so that the normal stdio routines can be used.
 */
void
SysVSubProc::setupSession(int modemFd)
{
    int fd;
    /*
     * Close everything down except the modem so
     * that the remote side doesn't get hung up on.
     */
    for (fd = Sys::getOpenMax()-1; fd >= 0; fd--)
	if (fd != modemFd)
	    Sys::close(fd);
    fclose(stdin);
    /*
     * Now make the line be the controlling tty
     * and create a new process group/session for
     * the login process that follows.
     */
    setsid();
#ifndef sco
    fd = Sys::open(getLine(), O_RDWR|O_NONBLOCK);
#else
    // NB: workaround kernel bug
    fd = Sys::open(getLine(), O_RDWR|O_NONBLOCK|O_NOCTTY);
#endif
    if (fd != STDIN_FILENO)
	fatal("Can not setup \"%s\" as stdin", getLine());
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) &~ O_NONBLOCK))
	fatal("Can not reset O_NONBLOCK: %m");
    Sys::close(modemFd);		// done with this, pitch it

    /*
     * Turn off CLOCAL so that SIGHUP is sent on modem disconnect.
     */
    struct termios term;
    if (tcgetattr(fd, &term) == 0) {
	term.c_cflag &= ~CLOCAL;
	tcsetattr(fd, TCSAFLUSH, &term);
    }
#ifdef TIOCSSOFTCAR
    /* turn the Solaris 2 soft carrier "feature" off */
    { int off = 0; ioctl(fd, TIOCSSOFTCAR, &off); }
#endif
    Getty::setupSession(fd);
}

/*
 * System V getty/login-specific subprocess support.
 */

SysVGetty::SysVGetty(const char* path, const fxStr& l, const fxStr& s) : SysVSubProc(path,l,s)
{
}

SysVGetty::~SysVGetty()
{
}

void
SysVGetty::setupSession(int modemFd)
{
    SysVSubProc::setupSession(modemFd);
    loginAccount();
}

void
SysVGetty::writeWtmp(utmp* ut)
{
    // append record of login to wtmp file
#if HAS_UTMPX
    updwtmpx(_PATH_WTMPX, ut);
#else
    int fd = Sys::open(_PATH_WTMP, O_WRONLY|O_APPEND);
    if (fd >= 0) {
	Sys::write(fd, (char *)ut, sizeof (*ut));
	Sys::close(fd);
    }
#endif
}

/*
 * Record the login session.
 */
void
SysVGetty::loginAccount()
{
    static utmp ut;			// zero unset fields
    ut.ut_pid = getpid();
    ut.ut_type = LOGIN_PROCESS;
#if HAS_UTEXIT
    ut.ut_exit.e_exit = 0;
    ut.ut_exit.e_termination = 0;
#endif
    ut.ut_time = Sys::now();
    // mark utmp entry as a login
    strncpy(ut.ut_user, "LOGIN", sizeof (ut.ut_user));
    /*
     * For SVR4 systems, use the trailing component of
     * the pathname to avoid problems where truncation
     * results in non-unique identifiers.
     */
    fxStr id(getLine());
    if (id.length() > sizeof (ut.ut_id))
	id.remove(0, id.length() - sizeof (ut.ut_id));
    strncpy(ut.ut_id, (char*) id, sizeof (ut.ut_id));
    strncpy(ut.ut_line, getLine(), sizeof (ut.ut_line));
    setutent();
    pututline(&ut);
    endutent();
    writeWtmp(&ut);
}

/*
 * Record the termination of login&co and
 * reset the state of the tty device.  Note
 * that this is called in the parent and
 * that we're entered with effective uid set
 * to the fax user and real uid of root.  Thus
 * we have to play games with uids in order
 * to write the utmp&wtmp entries, etc.
 */
void
SysVGetty::hangup()
{
    // at this point we're root and we can reset state
    struct utmp* ut;
    setutent();
    while ((ut = getutent()) != NULL) { 
	if (!strneq(ut->ut_line, getLine(), sizeof (ut->ut_line)))
	    continue;
	memset(ut->ut_user, 0, sizeof (ut->ut_user));
	ut->ut_type = DEAD_PROCESS;
#if HAS_UTEXIT
	ut->ut_exit.e_exit = (exitStatus >> 8) & 0xff;		// XXX
	ut->ut_exit.e_termination = exitStatus & 0xff;		// XXX
#endif
	ut->ut_time = Sys::now();
	pututline(ut);
	writeWtmp(ut);
	break;
    }
    endutent();
    Getty::hangup();
}

bool
SysVGetty::wait(int& status, bool block)
{
    if (Getty::wait(status, block)) {
	exitStatus = status;
	return (true);
    } else
	return (false);
}

/*
 * Public Interfaces.
 */
Getty*
OSnewGetty(const fxStr& dev, const fxStr& speed)
{
    return (new SysVGetty(_PATH_GETTY, dev, speed));
}

Getty*
OSnewVGetty(const fxStr& dev, const fxStr& speed)
{
    return (new SysVSubProc(_PATH_VGETTY, dev, speed));
}

Getty*
OSnewEGetty(const fxStr& dev, const fxStr& speed)
{
    return (new SysVSubProc(_PATH_EGETTY, dev, speed));
}
