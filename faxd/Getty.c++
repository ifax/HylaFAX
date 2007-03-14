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
#include "Getty.h"
#include "UUCPLock.h"

#include <termios.h>
#include <sys/param.h>

#include "Sys.h"

/*
 * FAX Server Getty Support Base Class.
 */

Getty::Getty(const char* p, const fxStr& l, const fxStr& s)
    : getty(p)
    , line(l)
    , speed(s)
    , tzVar("TZ")
    , langVar("LANG")
{
    pid = 0;
    argv[0] = NULL;
    argv[1] = NULL;
}

Getty::~Getty()
{
}

pid_t Getty::getPID() const		{ return pid; }
void Getty::setPID(pid_t p)		{ pid = p; }
const char* Getty::getLine() const	{ return line; }

static void
sigHUP(int)
{
    Sys::close(STDIN_FILENO);
    Sys::close(STDOUT_FILENO);
    Sys::close(STDERR_FILENO);
    sleep(5);
    _exit(1);
}

/*
 * Parse the getty argument string and
 * substitute runtime parameters:
 *
 *    %l	tty device name
 *    %s	current baud rate
 *
 * The resultant argv array is used to
 * exec getty below.
 */
void
Getty::setupArgv(const char* args, const fxStr& name, const fxStr& number)
{
    argbuf = args;
    nambuf = name;
    numbuf = number;
    bool insertName = false, insertNumber = false;
    u_int l;
    /*
     * Substitute escape sequences.
     */
    for (l = 0; l < argbuf.length();) {
	l = argbuf.next(l, '%');
	if (l >= argbuf.length()-1)
	    break;
	switch (argbuf[l+1]) {
	case 'l':			// %l = tty device name
	    argbuf.remove(l,2);
	    argbuf.insert(line, l);
	    l += line.length();		// avoid loops
	    break;
	case 's':			// %s = tty speed
	    argbuf.remove(l,2);
	    argbuf.insert(speed, l);
	    l += speed.length();	// avoid loops
	    break;
        case 'a':
            argbuf.remove(l-1,3);
            insertName = true;
            break;
        case 'u':
            argbuf.remove(l-1,3);
            insertNumber = true;
            break;            
	case '%':			// %% = %
	    argbuf.remove(l,1);
	    break;
	}
    }
    /*
     * Crack argument string and setup argv.
     */
    argv[0] = &getty[getty.nextR(getty.length(), '/')];
    u_int nargs = 1;
    for (l = 0; l < argbuf.length() && nargs < GETTY_MAXARGS-1;) {
	l = argbuf.skip(l, " \t");
	u_int token = l;
	l = argbuf.next(l, " \t");
	if (l > token) {
	    if (l < argbuf.length())
		argbuf[l++] = '\0';		// null terminate argument
	    argv[nargs++] = &argbuf[token];
	}
    }
    if (nargs < GETTY_MAXARGS-1 && insertName && nambuf.length()) 
        argv[nargs++] = &nambuf[0];
    if (nargs < GETTY_MAXARGS-1 && insertNumber && numbuf.length()) 
        argv[nargs++] = &numbuf[0];
    argv[nargs] = NULL;
}

fxStr
Getty::getCmdLine() const
{
    fxStr s(getty);

    for (u_int i = 1; argv[i] != NULL; i++) {
	s.append(' ');
	s.append(argv[i]);
    }
    return (s);
}

void
Getty::addEnvVar(int& envc, char* env[], fxStr& var)
{
    const char* val = getenv(var);
    if (val) {
        var.append(fxStr::format("=%s", val));
        const char* v = var;
        env[envc++] = (char*)v;
    }
}

/*
 * Setup a getty session and if successful exec the
 * getty program.  Note that this is always run in
 * the child.
 */
void
Getty::run(int fd, bool parentIsInit)
{
    if (Sys::chdir(_PATH_DEV) < 0)
	fatal("chdir: %m");
    /*
     * Reset signals known to be handled
     * by the current process (XXX).
     */
    signal(SIGTERM, fxSIGHANDLER(SIG_DFL));
    signal(SIGHUP, fxSIGHANDLER(sigHUP));
    /*
     * After the session is properly setup, the
     * stdio streams should be hooked to the tty
     * and the modem descriptor should be closed.
     */
    setupSession(fd);
    /*
     * If this getty is not being started from init
     * then pass a restricted environment.  Otherwise
     * just pass everything through.
     */
    if (!parentIsInit) {
        char* env[10];
        int envc = 0;
        addEnvVar(envc, env, tzVar);		// timezone
        addEnvVar(envc, env, langVar);		// for locale
        env[envc] = NULL;
        Sys::execve(getty, argv, env);	
    } else
        Sys::execv(getty, argv);
    _exit(127);
}

/*
 * Setup descriptors for stdout, and stderr.
 */
void
Getty::setupSession(int fd)
{
    struct stat sb;
    Sys::fstat(fd, sb);
#if HAS_FCHOWN
    (void) fchown(fd, 0, sb.st_gid);
#else
    Sys::chown(getLine(), 0, sb.st_gid);
#endif
#if HAS_FCHMOD
    fchmod(fd, 0622);
#else
    Sys::chmod(getLine(), 0622);
#endif
    if (dup2(fd, STDOUT_FILENO) < 0)
	fatal("Unable to dup stdin to stdout: %m");
    if (dup2(fd, STDERR_FILENO) < 0)
	fatal("Unable to dup stdin to stderr: %m");
}

bool
Getty::wait(int& status, bool block)
{
    return (Sys::waitpid(pid, status, block ? 0 : WNOHANG) == pid);
}

void
Getty::hangup()
{
    // NB: this is executed in the parent
    fxStr device = fxStr::format("%s" | line, _PATH_DEV);
    Sys::chown(device, UUCPLock::getUUCPUid(), UUCPLock::getUUCPGid());
    Sys::chmod(device, 0600);			// reset protection
}

extern void vlogError(const char* fmt, va_list ap);

void
Getty::fatal(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    hangup();
    va_end(ap);
}
