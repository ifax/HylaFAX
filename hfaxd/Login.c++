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
#include "HylaFAXServer.h"
#include "Sys.h"

#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#if HAS_CRYPT_H
#include <crypt.h>
#endif

void
HylaFAXServer::loginRefused(const char* why)
{
    if (++loginAttempts >= maxLoginAttempts) {
	reply(530, "Login incorrect (closing connection).");
	logNotice("Repeated login failures for user %s from %s [%s]"
	    , (const char*) the_user
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	);
	dologout(0);
    } else {
	reply(530, "User %s access denied.", (const char*) the_user);
	logNotice("HylaFAX LOGIN REFUSED (%s) FROM %s [%s], %s"
	    , why
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    }
}

/*
 * USER command. Sets global passwd state if named
 * account exists and is acceptable; sets askpasswd if a
 * PASS command is expected.  If logged in previously,
 * need to reset state.  User account must be accessible
 * from client host according to the contents of the
 * userAccessFile.
 */
void
HylaFAXServer::userCmd(const char* name)
{
    if (IS(LOGGEDIN)) {
	if (IS(PRIVILEGED) && the_user == name) {// revert to unprivileged mode
	    state &= ~S_PRIVILEGED;
	    reply(230, "User %s logged in.", name);
	    return;
	}
        end_login();
    }
    the_user = name;
    state &= ~S_PRIVILEGED;
    adminwd = "*";			// make sure no admin privileges
    passwd = "*";			// just in case...

    if (checkUser(name)) {
	if (passwd != "") {
	    state |= S_WAITPASS;
	    reply(331, "Password required for %s.", name);
	    /*
	     * Delay before reading passwd after first failed
	     * attempt to slow down password-guessing programs.
	     */
	    if (loginAttempts)
		sleep(loginAttempts);
	} else
	    login(230);
    } else
	loginRefused("user denied");
}

bool
HylaFAXServer::isAdminGroup(const char* user)
{
	bool retval = false;
	if (admingroup.length() > 0) {
	    int i;
	    static struct group* grinfo = getgrnam(admingroup);
	    const char *curruser = (user == NULL ? the_user.c_str() : user);
	    if (grinfo != NULL) {
		    for (i=0; grinfo->gr_mem[i] != NULL; i++) {
			    if (strcmp(curruser, grinfo->gr_mem[i]) == 0) retval = true;
		    }
	    }
	}
	return(retval);
}

void
HylaFAXServer::passCmd(const char* pass)
{
    if (IS(LOGGEDIN)) {
        reply(503, "Already logged in as USER %s.", (const char*) the_user);
        return;
    }
    if (!IS(WAITPASS)) {
        reply(503, "Login with USER first.");
        return;
    }
    state &= ~S_WAITPASS;

    /*
     * Disable long reply messages for old (broken) FTP
     * clients if the first character of the password
     * is a ``-''.
     */
    if (pass[0] == '-') {
	state &= ~S_LREPLIES;
	pass++;
    } else
	state |= S_LREPLIES;

    if (! checkPasswd(pass)) {
	if (++loginAttempts >= maxLoginAttempts) {
	    reply(530, "Login incorrect (closing connection).");
	    logNotice("Repeated login failures for user %s from %s [%s]"
		, (const char*) the_user
		, (const char*) remotehost
		, (const char*) remoteaddr
	    );
	    dologout(0);
	}
	reply(530, "Login incorrect.");
	logInfo("Login failed from %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
	return;
    }
    login(230);
}

/*
 * Login is shared between SNPP and HylaFAX RFC 959 protocol,
 * but different protocols use different codes for success.
 * We make the caller tell us what success is.
 */
void
HylaFAXServer::login(int code)
{
    loginAttempts = 0;		// this time successful
    state |= S_LOGGEDIN;

    uid_t ouid = geteuid();
    (void) seteuid(0);
    bool isSetup = (chroot(".") >= 0 && chdir("/") >= 0);
    /*
     * Install the client's fax-uid as the effective gid
     * so that created files automatically are given the
     * correct ``ownership'' (we store the owner's fax-uid
     * in the group-id field of the inode).
     */
    if (isSetup)
	(void) setegid(uid);
    (void) seteuid(ouid);
    if (!isSetup) {
	reply(550, "Cannot set privileges.");
	end_login();
	return;
    }

    (void) isShutdown(false);	// display any shutdown messages
    reply(code, "User %s logged in.", (const char*) the_user);
    if (TRACE(LOGIN))
	logInfo("FAX LOGIN FROM %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    (void) umask(077);
    if (tracingLevel & (TRACE_INXFERS|TRACE_OUTXFERS))
        xferfaxlog = Sys::open(xferfaxLogFile, O_WRONLY|O_APPEND|O_CREAT, 0600);

    initDefaultJob();		// setup connection-related state
    dirSetup();			// initialize directory handling
	if (isAdminGroup()) state |= S_PRIVILEGED;
}

void
HylaFAXServer::adminCmd(const char* pass)
{
    fxAssert(IS(LOGGEDIN), "ADMIN command permitted when not logged in");
    // NB: null adminwd is permitted
    if ((strcmp(crypt(pass, adminwd), adminwd) != 0) && !isAdminGroup()) {
	if (++adminAttempts >= maxAdminAttempts) {
	    reply(530, "Password incorrect (closing connection).");
	    logNotice("Repeated admin failures from %s [%s]"
		, (const char*) remotehost
		, (const char*) remoteaddr
	    );
	    dologout(0);
	} else {
	    reply(530, "Password incorrect.");
	    logInfo("ADMIN failed from %s [%s], %s"
		, (const char*) remotehost
		, (const char*) remoteaddr
		, (const char*) the_user
	    );
	}
	return;
    }
    if (TRACE(SERVER))
	logInfo("FAX ADMIN FROM %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    adminAttempts = 0;
    state |= S_PRIVILEGED;
    reply(230, "Administrative privileges established.");
}

/*
 * Terminate login as previous user, if any,
 * resetting state; used when USER command is
 * given or login fails.
 */
void
HylaFAXServer::end_login(void)
{
    if (IS(LOGGEDIN)) {
	uid_t ouid = geteuid();
	seteuid(0);
	seteuid(ouid);
    }
    state &= ~(S_LOGGEDIN|S_PRIVILEGED|S_WAITPASS);
    passwd = "*";
    adminwd = "*";
}

/*
 * Record logout in wtmp file, cleanup state,
 * and exit with supplied status.
 */
void
HylaFAXServer::dologout(int status)
{
    if (IS(LOGGEDIN))
	end_login();
    if (trigSpec != "") {
	fxStr emsg;
	cancelTrigger(emsg);
    }
    for (u_int i = 0, n = tempFiles.length(); i < n; i++)
	(void) Sys::unlink(tempFiles[i]);
    if (xferfaxlog != -1)
        Sys::close(xferfaxlog);
    if (clientFd != -1)
	Sys::close(clientFd);
    if (clientFIFOName != "") {
      /* we need to check for the FIFO since dologout() might be called
       * before we are chroot()ed... *sigh*
       */
      if (Sys::isFIFOFile( "/" | clientFIFOName)) {
          Sys::unlink("/" | clientFIFOName);
      } else if (Sys::isFIFOFile( FAX_SPOOLDIR "/" | clientFIFOName)) {
          Sys::unlink( FAX_SPOOLDIR "/" | clientFIFOName);
      }
    }
    for (JobDictIter iter(blankJobs); iter.notDone(); iter++) {
	Job* job = iter.value();
	fxStr file("/" | job->qfile);
	Sys::unlink(file);
    }
    _exit(status);		// beware of flushing buffers after a SIGPIPE
}
