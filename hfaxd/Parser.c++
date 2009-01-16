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
 * HylaFAX Client-Server Protocol Parser.
 */
#include "port.h"
#include "config.h"
#include "Sys.h"
#include "Socket.h"
#include "HylaFAXServer.h"
#include "Dispatcher.h"

#include <ctype.h>

#define	N(a)	(sizeof (a) / sizeof (a[0]))

/*
 * Standard protocol commands.
 */
static const tab cmdtab[] = {
{ "ABOR",         T_ABOR,	false, true, "[modem] (abort operation)" },
{ "ACCT",         T_ACCT,	false,false, "(specify account)" },
{ "ADMIN",        T_ADMIN,	 true, true, "password" },
{ "ALLO",         T_ALLO,	false,false, "(allocate disk space)" },
{ "ANSWER",       T_ANSWER,	 true, true, "modem [DATA|VOICE|FAX]" },
{ "APPE",         T_APPE,	 true, true, "file-name" },
{ "CWD",          T_CWD,	 true, true, "[directory-name]" },
{ "CDUP",         T_CDUP,	 true, true, "(change directory up one level)"},
{ "CHMOD",        T_CHMOD,	 true, true, "file-name mode" },
{ "CHOWN",        T_CHOWN,	 true, true, "file-name user" },
{ "DELE",         T_DELE,	 true, true, "file-name" },
{ "DISABLE",      T_DISABLE,	 true, true, "modem [reason]" },
{ "ENABLE",       T_ENABLE,	 true, true, "modem" },
{ "EPRT",         T_EPRT,	 true, true, "|f|addr|port|" },
{ "EPSV",         T_EPSV,	 true, true, "(set server in passive mode with extended result)" },
{ "HELP",         T_HELP,	false, true, "[<string>]" },
{ "FILEFMT",      T_FILEFMT,	 true, true, "[format-string]" },
{ "FILESORTFMT",  T_FILESFMT,	 true, true, "[format-string]" },
{ "FORM",         T_FORM,	 true, true, "format-type" },
{ "IDLE",         T_IDLE,	 true, true, "[max-idle-timeout]" },
{ "JDELE",        T_JDELE,	 true, true, "[job-id]" },
{ "JINTR",        T_JINTR,	 true, true, "[job-id]" },
{ "JKILL",        T_JKILL,	 true, true, "[job-id]" },
{ "JNEW",         T_JNEW,	 true, true, "" },
{ "JOB",          T_JOB,	 true, true, "[job-id]" },
{ "JOBFMT",       T_JOBFMT,	 true, true, "[format-string]" },
{ "JOBSORTFMT",   T_JOBSFMT,	 true, true, "[format-string]" },
{ "JPARM",        T_JPARM,	 true, true, "[parm-name [parm-value]]" },
{ "JREST",        T_JREST,	 true, true, "(reset current job state)" },
{ "JSUBM",        T_JSUB,	 true, true, "[job-id]" },
{ "JSUSP",        T_JSUSP,	 true, true, "[job-id]" },
{ "JWAIT",        T_JWAIT,	 true, true, "[job-id]" },
{ "JGDELE",       T_JGDELE,	 true,false, "[jobgroup-id]" },
{ "JGINTR",       T_JGINTR,	 true,false, "[jobgroup-id]" },
{ "JGKILL",       T_JGKILL,	 true,false, "[jobgroup-id]" },
{ "JGNEW",        T_JGNEW,	 true, true, "" },
{ "JGPARM",       T_JGPARM,	 true,false, "parm-name [parm-value]" },
{ "JGREST",       T_JGREST,	 true,false, "(reset current job group state)"},
{ "JGRP",         T_JGRP,	 true,false, "[jobgroup-id]" },
{ "JGSUBM",       T_JGSUB,	 true,false, "[jobgroup-id]" },
{ "JGSUSP",       T_JGSUSP,	 true,false, "[jobgroup-id]" },
{ "JGWAIT",       T_JGWAIT,	 true,false, "[jobgroup-id]" },
{ "LIST",         T_LIST,	 true, true, "[path-name]" },
{ "MDTM",         T_MDTM,	 true, true, "path-name" },
{ "MODE",         T_MODE,	false, true, "(specify transfer mode)" },
{ "MDMFMT",       T_MODEMFMT,	 true, true, "[format-string]" },
{ "MDMSORTFMT",   T_MODEMSFMT,	 true, true, "[format-string]" },
{ "NLST",         T_NLST,	 true, true, "[path-name]" },
{ "NOOP",         T_NOOP,	false, true, "" },
{ "PASS",         T_PASS,	false, true, "password" },
{ "PASV",         T_PASV,	 true, true, "(set server in passive mode)" },
{ "PORT",         T_PORT,	 true, true, "a0,a1,a2,a3,p0,p1" },
{ "PWD",          T_PWD,	 true, true, "(print working directory)" },
{ "QUIT",         T_QUIT,	false, true, "(terminate service)", },
{ "RCVFMT",       T_RCVFMT,	 true, true, "[format-string]" },
{ "RCVSORTFMT",   T_RCVSFMT,	 true, true, "[format-string]" },
{ "REIN",         T_REIN,	false, true, "(reinitialize server state)" },
{ "REST",         T_REST,	 true, true, "restart-marker" },
{ "RETP",         T_RETP,	 true, true, "file-name" },
{ "RETR",         T_RETR,	 true, true, "file-name" },
{ "RNFR",         T_RNFR,	 true,false, "file-name" },
{ "RNTO",         T_RNTO,	 true,false, "file-name" },
{ "SHUT",         T_SHUT,	 true, true, "NOW|HHSS|YYYYMMDDHHSS [reason]" },
{ "SITE",         T_SITE,	 true, true, "site-cmd [arguments]" },
{ "SIZE",         T_SIZE,	 true, true, "path-name" },
{ "STAT",         T_STAT,	false, true, "[path-name]" },
{ "STOR",         T_STOR,	 true, true, "file-name" },
{ "STOT",         T_STOT,	 true, true, "(store unique temporary)" },
{ "STOU",         T_STOU,	 true, true, "(store unique)" },
{ "STRU",         T_STRU,	false, true, "file-structure" },
{ "SYST",         T_SYST,	 true, true, "(return system type)" },
{ "TZONE",        T_TZONE,	 true, true, "[GMT|LOCAL]" },
{ "TYPE",         T_TYPE,	false, true, "transfer-type" },
{ "USER",         T_USER,	false, true, "username" },
{ "VRFY",         T_VRFY,	false, true, "dialstring" },
};

/*
 * Job parameter commands/keys.
 */
static const tab parmtab[] = {
{ "ACCTINFO",     T_ACCTINFO,	  false,false, "[<string>]" },
{ "BEGBR",        T_BEGBR,	  false, true, "[ANY|bit-rate]" },
{ "BEGST",        T_BEGST,	  false, true, "[ANY|scanline-time]" },
{ "CHOPTHRESHOLD",T_CHOPTHRESH,	  false, true, "[inches]" },
{ "CLIENT",       T_CLIENT,	  false, true, "[<string>]" },
{ "COMMENTS",     T_COMMENTS,	  false, true, "[<string>]" },
{ "COMMID",       T_COMMID,	  false, true, "(communication identifier)" },
{ "COVER",        T_COVER,	  false, true, "path-name" },
{ "DATAFORMAT",   T_DATAFORMAT,	  false, true, "[ANY|G31D|G32D|G4]" },
{ "DIALSTRING",   T_DIALSTRING,	  false, true, "[<string>]" },
{ "DOCUMENT",     T_DOCUMENT,	  false, true, "path-name" },
{ "DONEOP",       T_DONEOP,	  false, true, "[<string>]" },
{ "EXTERNAL",     T_EXTERNAL,	  false, true, "[<string>]" },
{ "ERRORCODE",    T_ERRORCODE,	  false, true, "[<string>]" },	// For compatiblity
{ "FAXNUMBER",    T_FAXNUMBER,	  false, true, "[<string>]" },
{ "FROMCOMPANY",  T_FROM_COMPANY, false, true, "[<string>]" },
{ "FROMLOCATION", T_FROM_LOCATION,false, true, "[<string>]" },
{ "FROMUSER",     T_FROM_USER,	  false, true, "[<string>]" },
{ "FROMVOICE",    T_FROM_VOICE,	  false, true, "[<string>]" },
{ "GROUPID",      T_GROUPID,	  false, true, "(job group identifier)" },
{ "HRES",         T_HRES,	  false,false, "[dots-per-inch]" },
{ "JOBID",        T_JOBID,	  false, true, "(job identifier)" },
{ "JOBINFO",      T_JOBINFO,	  false, true, "[<string>]" },
{ "JOBTYPE",      T_JOBTYPE,	  false, true, "(job type)" },
{ "LASTTIME",     T_LASTTIME,	  false, true, "[DDHHSS]" },
{ "MAXDIALS",     T_MAXDIALS,	  false, true, "[<number>]" },
{ "MAXPAGES",     T_MAXPAGES,	  false, true, "[<number>]" },
{ "MAXTRIES",     T_MAXTRIES,	  false, true, "[<number>]" },
{ "MINBR",        T_MINBR,	  false, true, "[ANY|bit-rate]" },
{ "MODEM",        T_MODEM,	  false, true, "[device|class]" },
{ "NDIALS",       T_NDIALS,	  false, true, "[<number>]" },
{ "NOTIFY",       T_NOTIFY,	  false, true, "[NONE|DONE|REQUEUE|DONE+REQUEUE]" },
{ "NOTIFYADDR",   T_NOTIFYADDR,	  false, true, "[email-address]" },
{ "NPAGES",       T_NPAGES,	  false, true, "[<number>]" },
{ "NTRIES",       T_NTRIES,	  false, true, "[<number>]" },
{ "OWNER",        T_OWNER,	  false, true, "[<name>|<number>]" },
{ "PAGECHOP",     T_PAGECHOP,	  false, true, "[DEFAULT|NONE|ALL|LAST]" },
{ "PAGELENGTH",   T_PAGELENGTH,	  false, true, "[millimeters]" },
{ "PAGERANGE",    T_PAGERANGE,	  false, true, "[<page range>]" },
{ "PAGEWIDTH",    T_PAGEWIDTH,	  false, true, "[millimeters]" },
{ "PASSWD",       T_PASSWD,	  false, true, "[<string>]" },
{ "POLL",         T_POLL,	  false, true, "selector [passwd]" },
{ "REGARDING",    T_REGARDING,	  false, true, "[<string>]" },
{ "RETRYTIME",    T_RETRYTIME,	  false, true, "[HHSS]" },
{ "SCHEDPRI",     T_SCHEDPRI,	  false, true, "[<number>]" },
{ "SENDTIME",     T_SENDTIME,	  false, true, "[NOW|YYYYMMDDHHSS]" },
{ "STATE",        T_STATE,	  false, true, "(job state)" },
{ "STATUS",       T_STATUS,       false, true, "[string]" },
{ "STATUSCODE",   T_STATUSCODE,   false, true, "[code]" },
{ "SUBADDR",      T_SUBADDR,	  false, true, "[<string>]" },
{ "TAGLINE",      T_TAGLINE,	  false, true, "[<string>]" },
{ "TOCOMPANY",    T_TO_COMPANY,	  false, true, "[<string>]" },
{ "TOLOCATION",   T_TO_LOCATION,  false, true, "[<string>]" },
{ "TOTDIALS",     T_TOTDIALS,	  false, true, "[<number>]" },
{ "TOTPAGES",     T_TOTPAGES,	  false, true, "[<number>]" },
{ "TOTTRIES",     T_TOTTRIES,	  false, true, "[<number>]" },
{ "TOUSER",       T_TO_USER,	  false, true, "[<string>]" },
{ "TOVOICE",      T_TO_VOICE,	  false, true, "[<string>]" },
{ "TSI",          T_TSI,	  false, true, "[<string>]" },
{ "USECONTCOVER", T_USE_CONTCOVER,false, true, "[YES|NO]" },
{ "USEXVRES",     T_USE_XVRES,	  false, true, "[YES|NO]" },
{ "USEECM",       T_USE_ECM,	  false, true, "[YES|NO]" },
{ "USETAGLINE",   T_USE_TAGLINE,  false, true, "[YES|NO]" },
{ "USRKEY",       T_USRKEY,	  false, true, "[<string>]" },
{ "VRES",         T_VRES,	  false, true, "[lines-per-inch]" },

{ "HELP",         T_HELP,	  false, true, "[<string>]" },
};

/*
 * Site-specific commands.
 */
static const tab sitetab[] = {
{ "ADDMODEM",     T_ADDMODEM,	  false,false, "modem [speed]"},
{ "ADDUSER",      T_ADDUSER,	  false, true, "user-spec [passwd [adminwd]]"},
{ "CONFIG",       T_CONFIG,	  false, true, "[parm-name [parm-value]]" },
{ "DELMODEM",     T_DELMODEM,	  false,false, "modem" },
{ "DELUSER",      T_DELUSER,	  false, true, "user-spec" },
{ "TRIGGER",	  T_TRIGGER,	  false, true, "spec" },
{ "HELP",         T_HELP,	  false, true, "[<string>]" },
{ "LOCKWAIT",     T_LOCKWAIT,	  false, true, "max-lockwait-timeout" },
};

static const tab*
lookup(const tab* p, u_int n, const char* cmd)
{
    while (n != 0) {
        if (strcmp(cmd, p->name) == 0)
            return (p);
	p++, n--;
    }
    return (NULL);
}
static const char*
tokenName(const tab* p, u_int n, Token t)
{
    while (n != 0) {
	if (p->token == t)
	    return (p->name);
	p++, n--;
    }
    return ("???");
}
const char*
HylaFAXServer::cmdToken(Token t)
{
    return tokenName(cmdtab, N(cmdtab), t);
}
const char*
HylaFAXServer::siteToken(Token t)
{
    return tokenName(sitetab, N(sitetab), t);
}
const char*
HylaFAXServer::parmToken(Token t)
{
    return tokenName(parmtab, N(parmtab), t);
}

u_int
HylaFAXServer::twodigits(const char* cp, u_int range)
{
    return ((cp[0]-'0')*10 + (cp[1]-'0')) % range;
}
u_int
HylaFAXServer::fourdigits(const char* cp)
{
    return (cp[0]-'0')*1000 +
	   (cp[1]-'0')*100 +
	   (cp[2]-'0')*10 +
	   (cp[3]-'0');
}
inline bool
isLoginToken(Token t)
{
    return (t == T_USER || t == T_PASS || t == T_ADMIN);
}

/*
 * Parse and process command input received on the
 * control channel.  This method is invoked whenever
 * data is present on the control channel.  We read
 * everything and parse (and execute) as much as possible
 * but do not block waiting for more data except when
 * a partial line of input is received.  This is done
 * to ensure other processing will be handled in a
 * timely fashion (e.g. processing of messages from
 * the scheduler received via the FIFO).
 */
int
HylaFAXServer::parse()
{
    if (IS(WAITDATA)) {			// recursive invocation
	state &= ~S_WAITDATA;
	return (0);
    }
					// stop control channel idle timeout
    Dispatcher::instance().stopTimer(this);
    pushToken(T_NIL);			// reset state
    for (;;) {
	/*
	 * Fetch the next complete line of input received on the
	 * control channel.  This call will fail when no more data
	 * is *currently* waiting on the control channel.  Note
	 * that this does not mean the connection has dropped; just
	 * that data is not available at this instant.  Note also
	 * that if a partial line of input is received a complete
	 * line will be waited for (see below).
	 */
	if (!getCmdLine(cbuf, sizeof (cbuf)))
	    break;
	/*
	 * Parse the line of input read above.
	 */
	if (getToken(T_STRING, "command token")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(cmdtab, N(cmdtab), tokenBody);
	    if (p == NULL)
		reply(500, "%s: Command not recognized.",
		    (const char*) tokenBody);
	    else if (!p->implemented)
		reply(502, "%s: Command not implemented.", p->name);
	    /*
	     * If the user is not privileged, then check for
	     * the service being shutdown if the command is
	     * not part of the login procedure (USER-PASS-ADMIN).
	     * This permits administrators to gain access to
	     * a system that is shutdown.  Other users will get
	     * dropped if they type something else.
	     */ 
	    else if (!IS(PRIVILEGED) && !isLoginToken(p->token) &&
	      isShutdown(!IS(LOGGEDIN))) {
		reply(221, "Server shutting down.  Goodbye.");
		dologout(0);		// XXX
	    /*
	     * If command requires client to be logged in check
	     * for this.  Note that some commands have variants
	     * that do not require the client be logged in--these
	     * cannot check here and must do it specially below.
	     */
	    } else if (p->checklogin && !checklogin(p->token))
		;
	    /*
	     * Command is valid, implemented, and the server
	     * is available to service it.  If the syntax is
	     * correct then reset the number of consecutive
	     * bad commands.  Note that since part of the syntax
	     * checking is login validation this logic will also
	     * catch people typing syntacitcally valid but
	     * unacceptable commands just to remain connected.
	     */
	    else if (cmd(p->token)) {
		if (p->token != T_REST)
		    restart_point = 0;
		consecutiveBadCmds = 0;
		continue;
	    }
	}
	/*
	 * If too many consecutive bad commands have been
	 * received disconnect.  This is to safeguard against
	 * a client that spews trash down the control connection.
	 */
	if (++consecutiveBadCmds >= maxConsecutiveBadCmds) {
	    /*
	     * Check for shutdown so that any shutdown message
	     * will get prepended to client reply.
	     */
	    (void) isShutdown(!IS(LOGGEDIN));
	    reply(221, "Server shutting down.  Goodbye.");
	    dologout(0);
	}
    }
    Dispatcher::instance().startTimer(idleTimeout, 0, this);
    return (0);
}

/*
 * Protocol command (one line).
 */
bool
HylaFAXServer::cmd(Token t)
{
    fxStr s;
    long n;

    switch (t) {
    case T_USER:			// user name
	if (string_param(s, "user name")) {
	    logcmd(t, "%s", (const char*) s);
	    userCmd(s);
	    return (true);
	}
	break;
    case T_PASS:			// user password
	if (string_param(s, "password")) {
	    logcmd(t, "<password>");
	    passCmd(s);
	    return (true);
	}
	break;
    case T_ADMIN:			// administrator password
	if (opt_CRLF())
	    s = "";
	else if (!string_param(s, "password"))
	    break;
	logcmd(t, "<password>");
	adminCmd(s);
	return (true);
    case T_REIN:			// reinitialize server
	logcmd(t);
	if (IS(LOGGEDIN))
	    (void) chdir("/");		// return to top of spooling area
	initServer();
	reply(220, "%s server (%s) ready.", (const char*) hostname, version);
	break;
    case T_REST:			// restart data transfer
	if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    restart_point = n;
	    reply(350, "Data transfer will restart at %lu, "
		"send transfer command", n);
	    return (true);
	}
	break;
    case T_SYST:			// system identification
	if (CRLF()) {
	    logcmd(t);
	    // this is what we *emulate*
	    reply(215, "%s", (const char*) systemType);
	    return (true);
	}
	break;
    case T_EPRT:			// extended port for data transfer
    case T_PORT:			// port for data transfer
	if (SPACE() && hostPort() && CRLF()) {
	    portCmd(t);
	    return (true);
	}
	break;
    case T_EPSV:			// extended passive mode
    case T_PASV:			// enable passive mode
	if (CRLF()) {
	    logcmd(t);
	    passiveCmd();
	    return (true);
	}
	break;
    case T_FORM:			// document format
	if (string_param(s, "document format")) {
	    logcmd(t, "%s", (const char*) s);
	    formCmd(s);
	    return (true);
	}
	break;
    case T_MODE:			// data transfer mode
	if (string_param(s, "transfer mode")) {
	    logcmd(t, "%s", (const char*) s);
	    modeCmd(s);
	    return (true);
	}
	break;
    case T_STRU:			// data transfer file structure
	if (string_param(s, "file structure")) {
	    logcmd(t, "%s", (const char*) s);
	    struCmd(s);
	    return (true);
	}
	break;
    case T_TYPE:			// data transfer type
	if (string_param(s, "transfer type")) {
	    logcmd(t, "%s", (const char*) s);
	    typeCmd(s);
	    return (true);
	}
	break;
    case T_RETP:			// retrieve next page of document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    retrievePageCmd(s);
	    return (true);
	}
	break;
    case T_RETR:			// retrieve document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    retrieveCmd(s);
	    return (true);
	}
	break;
    case T_STOR:			// store document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    // XXX file must exist if not admin, check for bad chars in pathname
	    storeCmd(s, "w");
	    return (true);
	}
	break;
    case T_APPE:			// append to document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    storeCmd(s, "a");
	    return (true);
	}
	break;
    case T_STOT:			// store uniquely named document/file
	if (CRLF()) {
	    logcmd(t);
	    storeUniqueCmd(true);
	    return (true);
	}
	break;
    case T_STOU:			// store uniquely named document/file
	if (CRLF()) {
	    logcmd(t);
	    storeUniqueCmd(false);
	    return (true);
	}
	break;
    case T_LIST:			// list directory/file
    case T_NLST:			// list directory/file names
	if (opt_CRLF()) {
	    logcmd(t);
	    if (t == T_LIST)
		listCmd(".");
	    else
		nlstCmd(".");
	    return (true);
	} else if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    if (t == T_LIST)
		listCmd(s);
	    else
		nlstCmd(s);
	    return (true);
	}
	break;
    case T_CWD:				// change working directory
	if (opt_CRLF()) {
	    logcmd(t);
	    cwdCmd("/");
	    return (true);
	} else if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    cwdCmd(s);
	    return (true);
	}
	break;
    case T_CDUP:			// CWD ..
	logcmd(t);
	cwdCmd("..");
	return (true);
    case T_PWD:				// print working directory
	if (CRLF()) {
	    logcmd(t);
	    pwdCmd();
	    return (true);
	}
	break;
    case T_STAT:			// stat file/server status
	if (opt_CRLF()) {
	    logcmd(t);
	    statusCmd();
	    return (true);
	} else if (checklogin(T_STAT) && pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    statFileCmd(s);
	    return (true);
	}
	break;
    case T_SIZE:			// return file size
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    struct stat sb;
	    if (Sys::stat(s, sb) < 0 || (sb.st_mode&S_IFMT) != S_IFREG)
		reply(550, "%s: not a plain file.", (const char*) s);
	    else
		reply(213, "%lu", (u_long) sb.st_size);
	    return (true);
	}
	break;
    case T_CHMOD:			// set file protection
	if (SPACE() && STRING(s, "filename") && SPACE() && NUMBER(n)) {
	    chmodCmd(s, (u_int) n);
	    return (true);
	}
	break;
    case T_MDTM:			// return file last modification time
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    mdtmCmd(s);
	    return (true);
	}
	break;
    case T_DELE:			// delete file/document
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    deleCmd(s);
	    return (true);
	}
	break;
    case T_ABOR:			// abort active command/incoming call
	if (opt_CRLF()) {
	    logcmd(t);
	    ack(225, cmdToken(t));
	    return (true);
	} else if (checkadmin(T_ABOR) && string_param(s, "modem")) {
	    abortCallCmd(s);
	    return (true);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    logcmd(t);
	    helpCmd(cmdtab, (char*) NULL);
	    return (true);
	} else if (string_param(s, "command name")) {
	    logcmd(t, "%s", (const char*) s);
	    s.raisecase();
	    if (s == "SITE")
		helpCmd(sitetab, NULL);
	    else if (s == "JPARM" || s == "JGPARM")
		helpCmd(parmtab, NULL);
	    else
		helpCmd(cmdtab, s);
	    return (true);
	}
	break;
    case T_JNEW:			// create new job
	if (CRLF()) {
	    newJobCmd();
	    return (true);
	}
	break;
    case T_JOB:				// select current job
	if (job_param(s)) {
	    setCurrentJob(s);
	    return (true);
	}
	break;
    case T_JREST:			// reset job state
	if (job_param(s)) {
	    resetJob(s);
	    return (true);
	}
	break;
    case T_JDELE:			// delete job
	if (job_param(s)) {
	    deleteJob(s);
	    return (true);
	}
	break;
    case T_JINTR:			// interrupt job
	if (job_param(s)) {
	    interruptJob(s);
	    return (true);
	}
	break;
    case T_JKILL:			// kill job
	if (job_param(s)) {
	    killJob(s);
	    return (true);
	}
	break;
    case T_JSUSP:			// suspend job
	if (job_param(s)) {
	    suspendJob(s);
	    return (true);
	}
	break;
    case T_JSUB:			// submit job
	if (job_param(s)) {
	    submitJob(s);
	    return (true);
	}
	break;
    case T_JWAIT:			// wait for job
	if (job_param(s)) {
	    waitForJob(s);
	    return (true);
	}
	break;
    case T_JGNEW:			// create job group
	if (CRLF()) {
	    curJob->groupid = curJob->jobid;
	    ack(200, cmdToken(t));
	    return (true);
	}
	break;
    case T_JGREST:			// reset current job group state
	if (CRLF()) {
	    return (true);
	}
	break;
    case T_JGRP:			// select current job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGDELE:			// delete job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGINTR:			// interrupt job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGKILL:			// kill job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGSUSP:			// suspend job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGSUB:			// submit job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_JGWAIT:			// wait for job group
	if (jgrp_param(s)) {
	    return (true);
	}
	break;
    case T_NOOP:			// no-op
	if (CRLF()) {
	    logcmd(t);
	    ack(200, cmdToken(t));
	    return (true);
	}
	break;
    case T_IDLE:			// set/query idle timeout
	if (opt_CRLF()) {
	    logcmd(t);
	    reply(213, "%u seconds.", idleTimeout);
	    return (true);
	} else if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    if ((unsigned)n > maxIdleTimeout && !IS(PRIVILEGED)) {
		idleTimeout = maxIdleTimeout;
		reply(213, "%lu: Idle timeout too large, set to %u.",
		    n, maxIdleTimeout);
	    } else {
		idleTimeout = (int) n;
		reply(213, "Idle timeout set to %u.", idleTimeout);
	    }
	    return (true);
	}
	break;
    case T_QUIT:			// terminate session
	if (CRLF()) {
	    logcmd(t);
	    reply(221, "Goodbye.");
	    dologout(0);
	    return (true);
	}
	break;
    case T_TZONE:			// set/query timezone usage
	if (opt_CRLF()) {
	    reply(200, "Time values are handled in %s.", 
		IS(USEGMT) ? "GMT" : tzname[0]);
	    return (true);
	} else if (string_param(s, "timezone specification")) {
	    logcmd(t, "%s", (const char*) s);
	    s.raisecase();
	    if (s == "GMT") {
		state |= S_USEGMT;
		reply(200, "Using time values in GMT.");
	    } else if (s == "LOCAL") {
		state &= ~S_USEGMT;
		reply(200, "Using time values in %s.", tzname[0]);
	    } else {
		reply(503, "Unknown timezone specification %s.",
		    (const char*) s);
	    }
	    return (true);
	}
	break;
    case T_JOBFMT:			// query/specify job format string
    case T_RCVFMT:			// query/specify recvq format string
    case T_MODEMFMT:			// query/specify modem format string
    case T_FILEFMT:			// query/specify file format string
	if (opt_CRLF()) {
	    reply(200, "%s",
		t == T_JOBFMT ?   (const char*) jobFormat :
		t == T_RCVFMT ?   (const char*) recvFormat:
		t == T_MODEMFMT ? (const char*) modemFormat :
				  (const char*) fileFormat
	    );
	    return (true);
	} else if (string_param(s, "format-string")) {
	    if (t == T_JOBFMT)
		jobFormat = s;
	    else if (t == T_RCVFMT)
		recvFormat = s;
	    else if (t == T_MODEMFMT)
		modemFormat = s;
	    else if (t == T_FILEFMT)
		fileFormat = s;
	    ack(200, cmdToken(t));
	    return (true);
	}
	break;
    case T_JOBSFMT:			// query/specify job format string
    case T_RCVSFMT:			// query/specify recvq format string
    case T_MODEMSFMT:			// query/specify modem format string
    case T_FILESFMT:			// query/specify file format string
	if (opt_CRLF()) {
	    reply(200, "%s",
		t == T_JOBSFMT ?   (const char*) jobSortFormat :
		t == T_RCVSFMT ?   (const char*) recvSortFormat:
		t == T_MODEMSFMT ? (const char*) modemSortFormat :
		                   (const char*) fileSortFormat
	    );
	    return (true);
	} else if (string_param(s, "format-string")) {
	    if (! allowSorting) {
		reply(530, "Local policy disables sorting.");
		break;
	    }
	    if (t == T_JOBSFMT)
		jobSortFormat = s;
	    else if (t == T_RCVSFMT)
		recvSortFormat = s;
	    else if (t == T_MODEMSFMT)
		modemSortFormat = s;
	    else if (t == T_FILESFMT)
		fileSortFormat = s;
	    ack(200, cmdToken(t));
	    return (true);
	}
	break;
    case T_JPARM:			// set/query job parameter
	if (opt_CRLF()) {
	    logcmd(T_JPARM);
	    if (checkJobState(curJob))
		jstatCmd(*curJob);
	    return (true);
	} else if (SPACE() && getToken(T_STRING, "parameter name")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(parmtab, N(parmtab), tokenBody);
	    if (p == NULL) {
		reply(500, "JPARM %s: Parameter not recognized.",
		    (const char*) tokenBody);
	    } else if (!p->implemented)
		reply(502, "JPARM %s: Parameter not implemented.", p->name);
	    else
		return (param_cmd(p->token));
	}
	break;
    case T_JGPARM:			// set/query job group parameter
	if (SPACE() && getToken(T_STRING, "parameter name")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(parmtab, N(parmtab), tokenBody);
	    if (p == NULL) {
		reply(500, "JPARM %s: Parameter not recognized.",
		    (const char*) tokenBody);
	    } else if (!p->implemented)
		reply(502, "JPARM %s: Parameter not implemented.", p->name);
	    else
		return (param_cmd(p->token));
	}
	break;
    case T_SITE:			// site-specific command
	if (SPACE() && getToken(T_STRING, "site command")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(sitetab, N(sitetab), tokenBody);
	    if (p == NULL) {
		reply(500, "SITE %s: Command not recognized.",
		    (const char*) tokenBody);
	    } else if (!p->implemented)
		reply(502, "SITE %s: Command not implemented.", p->name);
	    else
		return (site_cmd(p->token));
	}
	break;
    case T_ANSWER:			// answer phone on specific line
	if (checkadmin(T_ANSWER) && SPACE() && STRING(s, "modem")) {
	    fxStr how;
	    if (opt_CRLF())
		how = "any";
	    else if (!string_param(how, "answer-how"))
		break;
	    answerCallCmd(s, how);
	    return (true);
	}
	break;
    case T_CHOWN:			// assign file ownership
	if (checkadmin(T_CHOWN) && SPACE() && STRING(s, "filename")) {
	    fxStr who;
	    if (string_param(who, "user")) {
		chownCmd(s, who);
		return (true);
	    }
	}
	break;
    case T_DISABLE:			// disable outbound-usage of modem
	if (checkadmin(T_DISABLE) && SPACE() && STRING(s, "modem")) {
	    fxStr reason;
	    if (opt_CRLF())
		reason = "<unspecified reason>";
	    else if (!SPACE() || !multi_STRING(reason))
		break;
	    disableModemCmd(s, reason);
	    return (true);
	}
	break;
    case T_ENABLE:			// enable outbound-usage of modem
	if (checkadmin(T_ENABLE) && string_param(s, "modem")) {
	    enableModemCmd(s);
	    return (true);
	}
	break;
    case T_SHUT:			// shutdown server
	if (checkadmin(T_SHUT) && SPACE() && STRING(s, "shutdown-time")) {
	    fxStr reason;
	    if (opt_CRLF())
		reason = "<unspecified reason>";
	    else if (!SPACE() || !multi_STRING(reason))
		break;
	    const char* cp = s;
	    if (s.length() == 3 && strcasecmp(cp, "NOW") == 0) {
		time_t t = Sys::now();
		shutCmd(*localtime(&t), reason);
		return (true);
	    } else if (s.length() == 4 && checkNUMBER(cp)) {
		time_t t = Sys::now();
		struct tm tm = *localtime(&t);
		tm.tm_hour = 60*60*twodigits(cp, 60);
		tm.tm_min  =    60*twodigits(cp+2, 60);
		shutCmd(tm, reason);
		return (true);
	    } else if (s.length() == 12 && checkNUMBER(cp)) {
		struct tm tm;
		tm.tm_sec  = 0;
		tm.tm_min  = twodigits(cp+10, 60);
		tm.tm_hour = twodigits(cp+8, 24);
		tm.tm_mday = twodigits(cp+6, 31);
		tm.tm_mon  = twodigits(cp+4, 12) -1;
		tm.tm_year = fourdigits(cp+0) - 1900;
		shutCmd(tm, reason);
		return (true);
	    } else
		syntaxError("bad time specification");
	}
	break;
    case T_VRFY:			// verify server to use for destination
	if (string_param(s, "dialstring")) {
	    // XXX at least support a static file...
	    reply(200, "%s (%s)"
		, (const char*) hostname
		, (const char*) hostaddr
	    );
	    return (true);
	}
	break;
    default:
	break;
    }
    return (false);
}

/*
 * Site-specific protocol commands (one line).
 */
bool
HylaFAXServer::site_cmd(Token t)
{
    fxStr s;
    long n;

    switch (t) {
    case T_ADDUSER:
	if (checkadmin(T_ADDUSER) && SPACE() && STRING(s, "user-spec")) {
	    fxStr pass;
	    if (opt_CRLF()) {
		addUserCmd(s, "", "");
		return (true);
	    } else if (SPACE() && STRING(pass, "password")) {
		fxStr apass;
		if (opt_CRLF()) {
		    addUserCmd(s, pass, "");
		    return (true);
		} else if (string_param(apass, "admin-password")) {
		    addUserCmd(s, pass, apass);
		    return (true);
		}
	    }
	}
	break;
    case T_DELUSER:
	if (checkadmin(T_DELUSER) && string_param(s, "user-spec")) {
	    delUserCmd(s);
	    return (true);
	}
	break;
    case T_DELMODEM:
	if (checkadmin(T_DELMODEM) && string_param(s, "modem")) {
	    delModemCmd(s);
	    return (true);
	}
	break;
    case T_ADDMODEM:
	if (checkadmin(T_ADDMODEM) && string_param(s, "modem")) {
	    addModemCmd(s);
	    return (true);
	}
	break;
    case T_CONFIG:
	if (checkadmin(T_CONFIG) && SPACE() && STRING(s, "modem")) {
	    fxStr config;
	    if (opt_CRLF()) {
		configQueryCmd(s);
		return (true);
	    } else if (SPACE() && multi_STRING(config)) {
		configCmd(s, config);
		return (true);
	    }
	}
	break;
    case T_TRIGGER:
	if (string_param(s, "trigger-spec")) {
	    logcmd(t, "%s", (const char*)s);
	    triggerCmd("%s", (const char*) s);
	    return (true);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    helpCmd(sitetab, (char*) NULL);
	    return (true);
	} else if (string_param(s, "command name")) {
	    logcmd(T_SITE, "HELP %s", (const char*) s);
	    s.raisecase();
	    helpCmd(sitetab, s);
	    return (true);
	}
	break;
    case T_LOCKWAIT:
	if (opt_CRLF()) {
	    logcmd(t);
	    reply(213, "%u seconds.", lockTimeout);
	    return (true);
	} else if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    if ((unsigned)n > maxLockTimeout && !IS(PRIVILEGED)) {
		lockTimeout = maxLockTimeout;
		reply(213, "%lu: Lock timeout too large, set to %u.",
		    n, maxLockTimeout);
	    } else {
		lockTimeout = (int) n;
		reply(213, "Lock timeout set to %u.", lockTimeout);
	    }
	    return (true);
	}
    default:
	break;
    }
    return (false);
}

bool
HylaFAXServer::param_cmd(Token t)
{
    fxStr s;
    long n;
    time_t ticks;
    bool b;

    switch (t) {
    case T_SENDTIME:			// time to send job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (SPACE() && getToken(T_STRING, "time specification")) {
	    tokenBody.raisecase();
	    if (tokenBody == "NOW") {
		if (CRLF() && setJobParameter(*curJob, t, (time_t) 0)) {
		    reply(213, "%s set to NOW.", parmToken(t));
		    return (true);
		}
	    } else {
		pushToken(T_STRING);	// XXX
		if (TIMESPEC(12, ticks) && CRLF() &&
		  setJobParameter(*curJob, t, ticks)) {
		    if (ticks != 0) {
			const struct tm* tm = cvtTime(ticks);
			// XXX should response include seconds?
			reply(213, "%s set to %4d%02d%02d%02d%02d%02d."
			    , parmToken(t)
			    , tm->tm_year+1900
			    , tm->tm_mon+1
			    , tm->tm_mday
			    , tm->tm_hour
			    , tm->tm_min
			    , tm->tm_sec
			);
		    } else
			reply(213, "%s set to NOW.", parmToken(t));
		    return (true);
		}
	    }
	}
	break;
    case T_LASTTIME:			// time to kill job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (timespec_param(6, ticks) &&
	  setJobParameter(*curJob, t, ticks)) {
	    reply(213, "%s set to %02d%02d%02d."
		, parmToken(t)
		, ticks/(24*60*60)
		, (ticks/(60*60))%24
		, (ticks/60)%60
	    );
	    return (true);
	}
	break;
    case T_RETRYTIME:			// retry interval for job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (timespec_param(4, ticks) &&
	  setJobParameter(*curJob, t, ticks)) {
	    reply(213, "%s set to %02d%02d."
		, parmToken(t)
		, ticks/60
		, ticks%60
	    );
	    return (true);
	}
	break;
    case T_MAXDIALS:
    case T_MAXTRIES:
    case T_VRES:
    case T_PAGEWIDTH:
    case T_PAGELENGTH:
    case T_MINBR:
    case T_BEGBR:
    case T_BEGST:
    case T_MAXPAGES:
    case T_SCHEDPRI:
    case T_NPAGES:
    case T_TOTPAGES:
    case T_NTRIES:
    case T_TOTTRIES:
    case T_NDIALS:
    case T_TOTDIALS:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (number_param(n) &&
	  setJobParameter(*curJob, t, (u_short) n)) {
	    reply(213, "%s set to %u.", parmToken(t), n);
	    return (true);
	}
	break;
    case T_NOTIFYADDR:
    case T_NOTIFY:
    case T_MODEM:
    case T_DIALSTRING:
    case T_EXTERNAL:
    case T_SUBADDR:
    case T_DATAFORMAT:
    case T_TO_USER:
    case T_TO_LOCATION:
    case T_TO_COMPANY:
    case T_TO_VOICE:
    case T_FROM_USER:
    case T_FROM_LOCATION:
    case T_FROM_COMPANY:
    case T_FROM_VOICE:
    case T_USRKEY:
    case T_PASSWD:
    case T_CLIENT:
    case T_PAGECHOP:
    case T_TAGLINE:
    case T_GROUPID:
    case T_JOBID:
    case T_JOBINFO:
    case T_OWNER:
    case T_STATE:
    case T_STATUS:
    case T_STATUSCODE:
    case T_ERRORCODE:	// For compatibility
    case T_DONEOP:
    case T_COMMID:
    case T_REGARDING:
    case T_COMMENTS:
    case T_FAXNUMBER:
    case T_TSI:
    case T_PAGERANGE:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (SPACE() && multi_STRING(s) && CRLF() &&
	  setJobParameter(*curJob, t, s)) {
	    reply(213, "%s set to \"%s\".", parmToken(t), (const char*) s);
	    return (true);
	}
	break;
    case T_USE_ECM:
    case T_USE_TAGLINE:
    case T_USE_CONTCOVER:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (boolean_param(b) &&
	  setJobParameter(*curJob, t, b)) {
	    reply(213, "%s set to %s.", parmToken(t), b ? "YES" : "NO");
	    return (true);
	}
	break;
    case T_USE_XVRES:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (boolean_param(b) &&
	  setJobParameter(*curJob, t, b)) {
	    reply(213, "%s set to %s.", parmToken(t), b ? "YES" : "NO");
	    return (true);
	}
	break;
    case T_CHOPTHRESH:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (string_param(s, "floating point number") &&
	  setJobParameter(*curJob, t, (float) atof(s))) {
	    reply(213, "%s set to %g.", parmToken(t), (float) atof(s));
	    return (true);
	}
	break;
     case T_COVER:			// specify/query cover page document
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (pathname_param(s)) {
	    addCoverDocument(*curJob, s);
	    return (true);
	}
	break;
    case T_DOCUMENT:			// specify/query normal document
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (pathname_param(s)) {
	    addDocument(*curJob, s);
	    return (true);
	}
	break;
    case T_POLL:			// specify/query poll operation
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (true);
	} else if (SPACE() && STRING(s, "polling selector")) {
	    fxStr pwd;
	    if (opt_CRLF()) {
		addPollOp(*curJob, s, "");	// sep but no pwd
		return (true);
	    } else if (SPACE() && pwd_param(pwd) && CRLF()) {
		addPollOp(*curJob, s, pwd);	// sep & pwd
		return (true);
	    }
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    helpCmd(parmtab, (char*) NULL);
	    return (true);
	} else if (string_param(s, "parameter name")) {
	    s.raisecase();
	    logcmd(T_JPARM, "HELP %s", (const char*) s);
	    helpCmd(parmtab, s);
	    return (true);
	}
	break;
    default:
	break;
    }
    return (false);
}

/*
 * Collect a single string parameter.
 */
bool
HylaFAXServer::string_param(fxStr& s, const char* what)
{
    return SPACE() && STRING(s, what) && CRLF();
}

/*
 * Collect a single number parameter.
 */
bool
HylaFAXServer::number_param(long& n)
{
    return SPACE() && NUMBER(n) && CRLF();
}

/*
 * Collect a single boolean parameter.
 */
bool
HylaFAXServer::boolean_param(bool& b)
{
    return SPACE() && BOOLEAN(b) && CRLF();
}

/*
 * Collect a single time parameter.
 */
bool
HylaFAXServer::timespec_param(int ndigits, time_t& t)
{
    return SPACE() && TIMESPEC(ndigits, t) && CRLF();
}

/*
 * File manipulation command.
 */
bool
HylaFAXServer::pathname_param(fxStr& s)
{
    return SPACE() && pathname(s) && CRLF();
}

/*
 * Job control commands.
 */
bool
HylaFAXServer::job_param(fxStr& jid)
{
    if (opt_CRLF()) {
	jid = curJob->jobid;
	return (true);
    } else if (SPACE() && STRING(jid, "job identifer") && CRLF()) {
	jid.lowercase();
	return (true);
    }
    return (false);
}

/*
 * Job group control commands.
 */
bool
HylaFAXServer::jgrp_param(fxStr& jgid)
{
    if (opt_CRLF()) {
	jgid = curJob->groupid;
	return (true);
    } else if (SPACE() && STRING(jgid, "job group identifier") && CRLF()) {
	jgid.lowercase();
	return (true);
    }
    return (false);
}

bool
HylaFAXServer::pwd_param(fxStr& s)
{
    return STRING(s, "polling password");
}

bool
HylaFAXServer::pathname(fxStr& s)
{
    return STRING(s, "pathname");
}

bool
HylaFAXServer::TIMESPEC(u_int len, time_t& result)
{
    if (getToken(T_STRING, "time specification")) {
	if (tokenBody.length() == len) {
	    if (checkNUMBER(tokenBody)) {
		const char* cp = tokenBody;
		if (len == 12) {		// YYYYMMDDHHMM
		    struct tm tm;
		    tm.tm_sec  = 0;
		    tm.tm_min  = twodigits(cp+10, 60);
		    tm.tm_hour = twodigits(cp+8, 24);
		    tm.tm_mday = twodigits(cp+6, 32);
		    tm.tm_mon  = twodigits(cp+4, 13) - 1;
		    tm.tm_year = fourdigits(cp+0) - 1900;
		    tm.tm_isdst= -1;		// XXX not sure about this???
		    /*
		     * Client specifies time relative to GMT
		     * and mktime returns locally adjusted
		     * time so we need to adjust the result
		     * here to get things in the right timezone.
		     */
		    result = mktime(&tm) - gmtoff;
		} else if (len == 6) {		// DDHHMM
		    result = 24*60*60*twodigits(cp, 100)
			   +    60*60*twodigits(cp+2, 24)
			   +       60*twodigits(cp+4, 60);
		} else {			// MMSS
		    result = 60*twodigits(cp, 60)
			   +    twodigits(cp+2, 60);
		}
		return (true);
	    }
	} else
	    syntaxError(fxStr::format(
		"bad time specification (expecting %d digits)", len));
    }
    return (false);
}

bool
HylaFAXServer::BOOLEAN(bool& b)
{
    if (getToken(T_STRING, "boolean")) {
	tokenBody.raisecase();
	if (tokenBody == "YES") {
	    b = true;
	    return (true);
	} else if (tokenBody == "NO") {
	    b = false;
	    return (true);
	} else
	    syntaxError("invalid boolean value, use YES or NO");
    }
    return (false);
}

bool
HylaFAXServer::checkToken(Token wanted)
{
    Token t = nextToken();
    if (t != wanted) {
	pushToken(t);
	return (false);
    } else
	return (true);
}

bool
HylaFAXServer::getToken(Token wanted, const char* expected)
{
    Token t = nextToken();
    if (t != wanted) {
	if (t == T_LEXERR)
	    syntaxError("unmatched quote mark");
	else
	    syntaxError(fxStr::format("expecting %s", expected));
	return (false);
    } else
	return (true);
}

bool HylaFAXServer::SPACE()		{ return getToken(T_SP, "<SP>"); }
bool HylaFAXServer::COMMA()		{ return getToken(T_COMMA, "\",\""); }
bool HylaFAXServer::CRLF()		{ return getToken(T_CRLF, "<CRLF>"); }
bool HylaFAXServer::opt_CRLF()	{ return checkToken(T_CRLF); }

bool
HylaFAXServer::opt_STRING(fxStr& s)
{
    if (checkToken(T_STRING)) {
	s = tokenBody;
	return (true);
    } else
	return (false);
}
bool
HylaFAXServer::STRING(fxStr& s, const char* what)
{
    if (getToken(T_STRING, what != NULL ? what : "<STRING>")) {
	s = tokenBody;
	return (true);
    } else
	return (false);
}
bool
HylaFAXServer::multi_STRING(fxStr& s)
{
    if (!STRING(s))
	return (false);
    for (;;) {
	switch (nextToken()) {
	case T_CRLF:
	    pushToken(T_CRLF);
	    return (true);
	case T_COMMA:
	    s.append(',');
	    break;
	case T_STRING:
	    s.append(tokenBody);
	    break;
	case T_SP:
	    s.append(' ');
	    break;
	case T_LEXERR:
	    syntaxError("unmatched quote mark");
	    return (false);
	default:
	    break;
	}
    }
}

bool
HylaFAXServer::checkNUMBER(const char* cp)
{
    if (cp[0] == '-' || cp[0] == '+')
	cp++;
    for (; *cp; cp++)
	if (!isdigit(*cp)) {
	    syntaxError("invalid number");
	    return (false);
	}
    return (true);
}

bool
HylaFAXServer::NUMBER(long& n)
{
    if (!getToken(T_STRING, "decimal number")) {
	return (false);
    } else if (!checkNUMBER(tokenBody)) {
	return (false);
    } else {
	n = strtol(tokenBody, NULL, 0);
	return (true);
    }
}

void
HylaFAXServer::syntaxError(const char* msg)
{
    const char* cp = strchr(cbuf, '\0');
    if (cp[-1] == '\n')
	cp--;
    reply(500, "'%.*s': Syntax error, %s.", cp-cbuf, cbuf, msg);
}

/*
 * Lexical Scanner.
 */

/*
 * Return the next byte of data from the control channel.
 * If no data is available, either wait for new data to
 * be received or return EOF.
 */
int
HylaFAXServer::getChar(bool waitForInput)
{
    if (recvCC <= 0) {
	// enable non-blocking i/o on control channel
	(void) fcntl(STDIN_FILENO, F_SETFL, ctrlFlags | O_NONBLOCK);
again:
	do {
	    recvCC = Sys::read(STDIN_FILENO, recvBuf, sizeof (recvBuf));
	} while (recvCC < 0 && errno == EINTR);
	if (recvCC < 0 && errno == EWOULDBLOCK && waitForInput) {
	    Dispatcher& disp = Dispatcher::instance();
	    disp.startTimer(idleTimeout, 0, this);
	    for (state |= S_WAITDATA; IS(WAITDATA); disp.dispatch())
		;
	    disp.stopTimer(this);
	    goto again;
	}
	(void) fcntl(STDIN_FILENO, F_SETFL, ctrlFlags);
	if (recvCC <= 0) {
	    if (recvCC == 0) {	// no more input coming, remove handler
		Dispatcher::instance().unlink(STDIN_FILENO);
		dologout(0);		// XXX what about server interactions???
	    }
	    return (EOF);
	}
	recvNext = 0;
    }
    recvCC--;
    return recvBuf[recvNext++] & 0377;
}

/*
 * Push back control channel data.
 */
void
HylaFAXServer::pushCmdData(const char* data, int n)
{
    if ((unsigned) recvNext + n > sizeof (recvBuf)) {
	logError("No space to push back urgent data \"%.*s\"", n, data);
	return;
    }
    if (recvNext < n) {		// not enough space, copy existing data
	memmove(&recvBuf[n], &recvBuf[recvNext], recvCC);
	recvNext = 0;
    } else			// space available, just adjust recvNext
	recvNext -= n;
    memcpy(&recvBuf[recvNext], data, n);
    recvCC += n;
}

/*
 * Return the next line of data received on the control
 * channel, ignoring any Telnet protocol command sequences.
 */
bool
HylaFAXServer::getCmdLine(char* s, int n, bool waitForInput)
{
    char* cp = s;
    cpos = 0;
    while (n > 1) {
	int c = getChar(cp != s || waitForInput);
	if (c == EOF)
	    return (false);
	if (c == IAC) {			// telnet protocol command
	    c = getChar(true);
	    if (c == EOF)
		return (false);
	    switch (c&0377) {
	    case WILL:
	    case WONT:
		c = getChar(true);	// telnet option
		if (c == EOF)
		    return (false);
		printf("%c%c%c", IAC, DONT, c&0377);
		(void) fflush(stdout);
		continue;
	    case DO:
	    case DONT:
		c = getChar(true);	// telnet option
		if (c == EOF)
		    return (false);
		printf("%c%c%c", IAC, WONT, c&0377);
		(void) fflush(stdout);
		continue;
	    case IAC:			// <IAC><IAC> -> <IAC>
		break;
	    default:   			// ignore command
		continue;
	    }
	}
	if (c == '\n') {
	    // convert \r\n -> \n
	    if (cp > s && cp[-1] == '\r')
		cp[-1] = '\n';
	    else
		*cp++ = '\n', n--;
	    break;
	}
	*cp++ = c, n--;
    }
    *cp = '\0';
    if (TRACE(PROTOCOL))
        logDebug("command: %s", s);
    return (true);
}

void
HylaFAXServer::timerExpired(long, long)
{
    reply(421,
      "Timeout (%d seconds): closing control connection.", idleTimeout);
    if (TRACE(SERVER)) {
	time_t now = Sys::now();
	logInfo("User %s timed out after %d seconds at %.24s"
	    , IS(LOGGEDIN) ? (const char*) the_user : "(noone)"
	    , idleTimeout
	    , asctime(cvtTime(now))
	);
    }
    dologout(1);
}

Token
HylaFAXServer::nextToken(void)
{
    if (pushedToken != T_NIL) {
	Token t = pushedToken;
	pushedToken = T_NIL;
	return (t);
    }
    switch (cbuf[cpos]) {
    case ' ':
	while (cbuf[++cpos] == ' ')		// compress multiple spaces
	    ;
	return (T_SP);
    case '\n':
	cpos++;
	return (T_CRLF);
    case ',':
	cpos++;
	return (T_COMMA);
    case '"':
	/*
	 * Parse quoted string and deal with \ escapes.
	 */
	tokenBody.resize(0);
	for (;;) {
	    int c = cbuf[++cpos];
	    if (c == '"') {
		cpos++;
		return (T_STRING);
	    }
	    if (c == '\n')			// unmatched quote mark
		return (T_LEXERR);
	    if (c == '\\') {			// \ escape handling
		c = cbuf[++cpos];
		if (isdigit(c)) {		// \nnn octal escape
		    int v = c - '0';
		    if (isdigit(c = cbuf[cpos+1])) {
			cpos++, v = (v << 3) + (c - '0');
			if (isdigit(c = cbuf[cpos+1]))
			    cpos++, v = (v << 3) + (c - '0');
		    }
		    c = v;
		}
	    }
	    tokenBody.append(c);
	}
	/*NOTREACHED*/
    }
    int base = cpos;
    do {
	cpos++;
    } while (cbuf[cpos] != ' ' && cbuf[cpos] != '\n' && cbuf[cpos] != ',');
    tokenBody = fxStr(&cbuf[base], cpos - base);
    return (T_STRING);
}

void
HylaFAXServer::helpCmd(const tab* ctab, const char* s)
{
    const char* type;
    u_int NCMDS;
    if (ctab == sitetab) {
        type = "SITE ";
	NCMDS = N(sitetab);
    } else if (ctab == parmtab) {
	type = "JPARM ";
	NCMDS = N(parmtab);
    } else {
        type = "";
	NCMDS = N(cmdtab);
    }
    int width = 0;
    const tab* c = ctab;
    for (u_int n = NCMDS; n != 0; c++, n--) {
        int len = strlen(c->name);
        if (len > width)
            width = len;
    }
    width = (width + 8) &~ 7;
    if (s == NULL) {
        lreply(214, "The following %scommands are recognized %s.",
            type, "(* =>'s unimplemented)");
        int columns = 76 / width;
        if (columns == 0)
            columns = 1;
        int lines = (NCMDS + columns - 1) / columns;
        for (int i = 0; i < lines; i++) {
            printf("   ");
            for (int j = 0; j < columns; j++) {
                c = &ctab[j*lines + i];
                printf("%s%c", c->name, !c->implemented ? '*' : ' ');
                if (c + lines >= &ctab[NCMDS])
                    break;
                int w = strlen(c->name) + 1;
                while (w < width) {
                    putchar(' ');
                    w++;
                }
            }
            printf("\r\n");
        }
        (void) fflush(stdout);
        reply(214, "Direct comments to %s.", (const char*) faxContact);
        return;
    }
    c = lookup(ctab, NCMDS, s);
    if (c == NULL) {
        reply(502, "Unknown command %s.", s);
        return;
    }
    if (c->implemented)
        reply(214, "Syntax: %s%s %s", type, c->name, c->help);
    else
        reply(214, "%s%-*s\t%s; unimplemented.", type, width, c->name, c->help);
}

void
HylaFAXServer::logcmd(Token t, const char* fmt ...)
{
    if (TRACE(PROTOCOL)) {
	const char* name = cmdToken(t);
	if (!name)
	    name = siteToken(t);
	if (!name)
	    name = "???";
	if (fmt != NULL) {
	    va_list ap;
	    va_start(ap, fmt);
	    vlogInfo(fxStr::format("%s %s", name, fmt), ap);
	    va_end(ap);
	} else
	    logInfo("%s", name);
    }
}

void
HylaFAXServer::cmdFailure(Token t, const char* why)
{
    if (TRACE(SERVER)) {
	const char* cp = cmdToken(t);
	if (!cp)
	    cp = siteToken(t);
	if (cp)
	    logInfo("%s cmd failure - %s", cp, why);
	else
	    logInfo("#%u cmd failure - %s", t, why);
    }
}

bool
HylaFAXServer::checklogin(Token t)
{
    if (!IS(LOGGEDIN)) {
	cmdFailure(t, "not logged in");
	reply(530, "Please login with USER and PASS.");
	return (false);
    } else
	return (true);
}

bool
HylaFAXServer::checkadmin(Token t)
{
    if (checklogin(t)) {
	if (IS(PRIVILEGED))
	    return (true);
	cmdFailure(t, "user is not privileged");
	reply(530, "Please use ADMIN to establish administrative privileges.");
    }
    return (false);
}
