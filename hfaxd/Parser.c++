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
{ "ABOR",         T_ABOR,	FALSE, TRUE, "[modem] (abort operation)" },
{ "ACCT",         T_ACCT,	FALSE,FALSE, "(specify account)" },
{ "ADMIN",        T_ADMIN,	 TRUE, TRUE, "password" },
{ "ALLO",         T_ALLO,	FALSE,FALSE, "(allocate disk space)" },
{ "ANSWER",       T_ANSWER,	 TRUE, TRUE, "modem [DATA|VOICE|FAX]" },
{ "APPE",         T_APPE,	 TRUE, TRUE, "file-name" },
{ "CWD",          T_CWD,	 TRUE, TRUE, "[directory-name]" },
{ "CDUP",         T_CDUP,	 TRUE, TRUE, "(change directory up one level)"},
{ "CHMOD",        T_CHMOD,	 TRUE, TRUE, "file-name mode" },
{ "CHOWN",        T_CHOWN,	 TRUE, TRUE, "file-name user" },
{ "DELE",         T_DELE,	 TRUE, TRUE, "file-name" },
{ "DISABLE",      T_DISABLE,	 TRUE, TRUE, "modem [reason]" },
{ "ENABLE",       T_ENABLE,	 TRUE, TRUE, "modem" },
{ "HELP",         T_HELP,	FALSE, TRUE, "[<string>]" },
{ "FILEFMT",      T_FILEFMT,	 TRUE, TRUE, "[format-string]" },
{ "FORM",         T_FORM,	 TRUE, TRUE, "format-type" },
{ "IDLE",         T_IDLE,	 TRUE, TRUE, "[max-idle-timeout]" },
{ "JDELE",        T_JDELE,	 TRUE, TRUE, "[job-id]" },
{ "JINTR",        T_JINTR,	 TRUE, TRUE, "[job-id]" },
{ "JKILL",        T_JKILL,	 TRUE, TRUE, "[job-id]" },
{ "JNEW",         T_JNEW,	 TRUE, TRUE, "" },
{ "JOB",          T_JOB,	 TRUE, TRUE, "[job-id]" },
{ "JOBFMT",       T_JOBFMT,	 TRUE, TRUE, "[format-string]" },
{ "JPARM",        T_JPARM,	 TRUE, TRUE, "[parm-name [parm-value]]" },
{ "JREST",        T_JREST,	 TRUE, TRUE, "(reset current job state)" },
{ "JSUBM",        T_JSUB,	 TRUE, TRUE, "[job-id]" },
{ "JSUSP",        T_JSUSP,	 TRUE, TRUE, "[job-id]" },
{ "JWAIT",        T_JWAIT,	 TRUE, TRUE, "[job-id]" },
{ "JGDELE",       T_JGDELE,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGINTR",       T_JGINTR,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGKILL",       T_JGKILL,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGNEW",        T_JGNEW,	 TRUE, TRUE, "" },
{ "JGPARM",       T_JGPARM,	 TRUE,FALSE, "parm-name [parm-value]" },
{ "JGREST",       T_JGREST,	 TRUE,FALSE, "(reset current job group state)"},
{ "JGRP",         T_JGRP,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGSUBM",       T_JGSUB,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGSUSP",       T_JGSUSP,	 TRUE,FALSE, "[jobgroup-id]" },
{ "JGWAIT",       T_JGWAIT,	 TRUE,FALSE, "[jobgroup-id]" },
{ "LIST",         T_LIST,	 TRUE, TRUE, "[path-name]" },
{ "MDTM",         T_MDTM,	 TRUE, TRUE, "path-name" },
{ "MODE",         T_MODE,	FALSE, TRUE, "(specify transfer mode)" },
{ "MDMFMT",       T_MODEMFMT,	 TRUE, TRUE, "[format-string]" },
{ "NLST",         T_NLST,	 TRUE, TRUE, "[path-name]" },
{ "NOOP",         T_NOOP,	FALSE, TRUE, "" },
{ "PASS",         T_PASS,	FALSE, TRUE, "password" },
{ "PASV",         T_PASV,	 TRUE, TRUE, "(set server in passive mode)" },
{ "PORT",         T_PORT,	 TRUE, TRUE, "a0,a1,a2,a3,p0,p1" },
{ "PWD",          T_PWD,	 TRUE, TRUE, "(print working directory)" },
{ "QUIT",         T_QUIT,	FALSE, TRUE, "(terminate service)", },
{ "RCVFMT",       T_RCVFMT,	 TRUE, TRUE, "[format-string]" },
{ "REIN",         T_REIN,	FALSE, TRUE, "(reinitialize server state)" },
{ "REST",         T_REST,	 TRUE, TRUE, "restart-marker" },
{ "RETP",         T_RETP,	 TRUE, TRUE, "file-name" },
{ "RETR",         T_RETR,	 TRUE, TRUE, "file-name" },
{ "RNFR",         T_RNFR,	 TRUE,FALSE, "file-name" },
{ "RNTO",         T_RNTO,	 TRUE,FALSE, "file-name" },
{ "SHUT",         T_SHUT,	 TRUE, TRUE, "NOW|HHSS|YYYYMMDDHHSS [reason]" },
{ "SITE",         T_SITE,	 TRUE, TRUE, "site-cmd [arguments]" },
{ "SIZE",         T_SIZE,	 TRUE, TRUE, "path-name" },
{ "STAT",         T_STAT,	FALSE, TRUE, "[path-name]" },
{ "STOR",         T_STOR,	 TRUE, TRUE, "file-name" },
{ "STOT",         T_STOT,	 TRUE, TRUE, "(store unique temporary)" },
{ "STOU",         T_STOU,	 TRUE, TRUE, "(store unique)" },
{ "STRU",         T_STRU,	FALSE, TRUE, "file-structure" },
{ "SYST",         T_SYST,	 TRUE, TRUE, "(return system type)" },
{ "TZONE",        T_TZONE,	 TRUE, TRUE, "[GMT|LOCAL]" },
{ "TYPE",         T_TYPE,	FALSE, TRUE, "transfer-type" },
{ "USER",         T_USER,	FALSE, TRUE, "username" },
{ "VRFY",         T_VRFY,	FALSE, TRUE, "dialstring" },
};

/*
 * Job parameter commands/keys.
 */
static const tab parmtab[] = {
{ "ACCTINFO",     T_ACCTINFO,	  FALSE,FALSE, "[<string>]" },
{ "BEGBR",        T_BEGBR,	  FALSE, TRUE, "[ANY|bit-rate]" },
{ "BEGST",        T_BEGST,	  FALSE, TRUE, "[ANY|scanline-time]" },
{ "CHOPTHRESHOLD",T_CHOPTHRESH,	  FALSE, TRUE, "[inches]" },
{ "CLIENT",       T_CLIENT,	  FALSE, TRUE, "[<string>]" },
{ "COMMENTS",     T_COMMENTS,	  FALSE,FALSE, "[<string>]" },
{ "COMMID",       T_COMMID,	  FALSE, TRUE, "(communication identifier)" },
{ "COVER",        T_COVER,	  FALSE, TRUE, "path-name" },
{ "DATAFORMAT",   T_DATAFORMAT,	  FALSE, TRUE, "[ANY|G31D|G32D|G4]" },
{ "DIALSTRING",   T_DIALSTRING,	  FALSE, TRUE, "[<string>]" },
{ "DOCUMENT",     T_DOCUMENT,	  FALSE, TRUE, "path-name" },
{ "DONEOP",       T_DONEOP,	  FALSE, TRUE, "[<string>]" },
{ "EXTERNAL",     T_EXTERNAL,	  FALSE, TRUE, "[<string>]" },
{ "FROMCOMPANY",  T_FROM_COMPANY, FALSE,FALSE, "[<string>]" },
{ "FROMLOCATION", T_FROM_LOCATION,FALSE,FALSE, "[<string>]" },
{ "FROMUSER",     T_FROM_USER,	  FALSE, TRUE, "[<string>]" },
{ "FROMVOICE",    T_FROM_VOICE,	  FALSE,FALSE, "[<string>]" },
{ "GROUPID",      T_GROUPID,	  FALSE, TRUE, "(job group identifier)" },
{ "HRES",         T_HRES,	  FALSE,FALSE, "[dots-per-inch]" },
{ "JOBID",        T_JOBID,	  FALSE, TRUE, "(job identifier)" },
{ "JOBINFO",      T_JOBINFO,	  FALSE, TRUE, "[<string>]" },
{ "JOBTYPE",      T_JOBTYPE,	  FALSE, TRUE, "(job type)" },
{ "LASTTIME",     T_LASTTIME,	  FALSE, TRUE, "[DDHHSS]" },
{ "MAXDIALS",     T_MAXDIALS,	  FALSE, TRUE, "[<number>]" },
{ "MAXPAGES",     T_MAXPAGES,	  FALSE, TRUE, "[<number>]" },
{ "MAXTRIES",     T_MAXTRIES,	  FALSE, TRUE, "[<number>]" },
{ "MINBR",        T_MINBR,	  FALSE, TRUE, "[ANY|bit-rate]" },
{ "MODEM",        T_MODEM,	  FALSE, TRUE, "[device|class]" },
{ "NDIALS",       T_NDIALS,	  FALSE, TRUE, "[<number>]" },
{ "NOTIFY",       T_NOTIFY,	  FALSE, TRUE, "[NONE|DONE|REQUEUE|DONE+REQUEUE]" },
{ "NOTIFYADDR",   T_NOTIFYADDR,	  FALSE, TRUE, "[email-address]" },
{ "NPAGES",       T_NPAGES,	  FALSE, TRUE, "[<number>]" },
{ "NTRIES",       T_NTRIES,	  FALSE, TRUE, "[<number>]" },
{ "OWNER",        T_OWNER,	  FALSE, TRUE, "[<name>|<number>]" },
{ "PAGECHOP",     T_PAGECHOP,	  FALSE, TRUE, "[DEFAULT|NONE|ALL|LAST]" },
{ "PAGELENGTH",   T_PAGELENGTH,	  FALSE, TRUE, "[millimeters]" },
{ "PAGEWIDTH",    T_PAGEWIDTH,	  FALSE, TRUE, "[millimeters]" },
{ "PASSWD",       T_PASSWD,	  FALSE, TRUE, "[<string>]" },
{ "POLL",         T_POLL,	  FALSE, TRUE, "selector [passwd]" },
{ "REGARDING",    T_REGARDING,	  FALSE,FALSE, "[<string>]" },
{ "RETRYTIME",    T_RETRYTIME,	  FALSE, TRUE, "[HHSS]" },
{ "SCHEDPRI",     T_SCHEDPRI,	  FALSE, TRUE, "[<number>]" },
{ "SENDTIME",     T_SENDTIME,	  FALSE, TRUE, "[NOW|YYYYMMDDHHSS]" },
{ "STATE",        T_STATE,	  FALSE, TRUE, "(job state)" },
{ "STATUS",       T_STATUS,       FALSE, TRUE, "[<string>" },
{ "SUBADDR",      T_SUBADDR,	  FALSE, TRUE, "[<string>]" },
{ "TAGLINE",      T_TAGLINE,	  FALSE, TRUE, "[<string>]" },
{ "TOCOMPANY",    T_TO_COMPANY,	  FALSE, TRUE, "[<string>]" },
{ "TOLOCATION",   T_TO_LOCATION,  FALSE, TRUE, "[<string>]" },
{ "TOTDIALS",     T_TOTDIALS,	  FALSE, TRUE, "[<number>]" },
{ "TOTPAGES",     T_TOTPAGES,	  FALSE, TRUE, "[<number>]" },
{ "TOTTRIES",     T_TOTTRIES,	  FALSE, TRUE, "[<number>]" },
{ "TOUSER",       T_TO_USER,	  FALSE, TRUE, "[<string>]" },
{ "TOVOICE",      T_TO_VOICE,	  FALSE,FALSE, "[<string>]" },
{ "USECONTCOVER", T_USE_CONTCOVER,FALSE, TRUE, "[YES|NO]" },
{ "USEECM",       T_USE_ECM,	  FALSE, TRUE, "[YES|NO]" },
{ "USETAGLINE",   T_USE_TAGLINE,  FALSE, TRUE, "[YES|NO]" },
{ "USRKEY",       T_USRKEY,	  FALSE, TRUE, "[<string>]" },
{ "VRES",         T_VRES,	  FALSE, TRUE, "[lines-per-inch]" },

{ "HELP",         T_HELP,	  FALSE, TRUE, "[<string>]" },
};

/*
 * Site-specific commands.
 */
static const tab sitetab[] = {
{ "ADDMODEM",     T_ADDMODEM,	  FALSE,FALSE, "modem [speed]"},
{ "ADDUSER",      T_ADDUSER,	  FALSE, TRUE, "user-spec [passwd [adminwd]]"},
{ "CONFIG",       T_CONFIG,	  FALSE, TRUE, "[parm-name [parm-value]]" },
{ "DELMODEM",     T_DELMODEM,	  FALSE,FALSE, "modem" },
{ "DELUSER",      T_DELUSER,	  FALSE, TRUE, "user-spec" },
{ "TRIGGER",	  T_TRIGGER,	  FALSE, TRUE, "spec" },
{ "HELP",         T_HELP,	  FALSE, TRUE, "[<string>]" },
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
inline fxBool
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
fxBool
HylaFAXServer::cmd(Token t)
{
    fxStr s;
    long n;

    switch (t) {
    case T_USER:			// user name
	if (string_param(s, "user name")) {
	    logcmd(t, "%s", (const char*) s);
	    userCmd(s);
	    return (TRUE);
	}
	break;
    case T_PASS:			// user password
	if (string_param(s, "password")) {
	    logcmd(t, "<password>");
	    passCmd(s);
	    return (TRUE);
	}
	break;
    case T_ADMIN:			// administrator password
	if (opt_CRLF())
	    s = "";
	else if (!string_param(s, "password"))
	    break;
	logcmd(t, "<password>");
	adminCmd(s);
	return (TRUE);
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
	    return (TRUE);
	}
	break;
    case T_SYST:			// system identification
	if (CRLF()) {
	    logcmd(t);
	    // this is what we *emulate*
	    reply(215, "%s", (const char*) systemType);
	    return (TRUE);
	}
	break;
    case T_PORT:			// port for data transfer
	if (SP() && hostPort() && CRLF()) {
	    portCmd();
	    return (TRUE);
	}
	break;
    case T_PASV:			// enable passive mode
	if (CRLF()) {
	    logcmd(t);
	    passiveCmd();
	    return (TRUE);
	}
	break;
    case T_FORM:			// document format
	if (string_param(s, "document format")) {
	    logcmd(t, "%s", (const char*) s);
	    formCmd(s);
	    return (TRUE);
	}
	break;
    case T_MODE:			// data transfer mode
	if (string_param(s, "transfer mode")) {
	    logcmd(t, "%s", (const char*) s);
	    modeCmd(s);
	    return (TRUE);
	}
	break;
    case T_STRU:			// data transfer file structure
	if (string_param(s, "file structure")) {
	    logcmd(t, "%s", (const char*) s);
	    struCmd(s);
	    return (TRUE);
	}
	break;
    case T_TYPE:			// data transfer type
	if (string_param(s, "transfer type")) {
	    logcmd(t, "%s", (const char*) s);
	    typeCmd(s);
	    return (TRUE);
	}
	break;
    case T_RETP:			// retrieve next page of document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    retrievePageCmd(s);
	    return (TRUE);
	}
	break;
    case T_RETR:			// retrieve document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    retrieveCmd(s);
	    return (TRUE);
	}
	break;
    case T_STOR:			// store document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    // XXX file must exist if not admin, check for bad chars in pathname
	    storeCmd(s, "w");
	    return (TRUE);
	}
	break;
    case T_APPE:			// append to document/file
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    storeCmd(s, "a");
	    return (TRUE);
	}
	break;
    case T_STOT:			// store uniquely named document/file
	if (CRLF()) {
	    logcmd(t);
	    storeUniqueCmd(TRUE);
	    return (TRUE);
	}
	break;
    case T_STOU:			// store uniquely named document/file
	if (CRLF()) {
	    logcmd(t);
	    storeUniqueCmd(FALSE);
	    return (TRUE);
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
	    return (TRUE);
	} else if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    if (t == T_LIST)
		listCmd(s);
	    else
		nlstCmd(s);
	    return (TRUE);
	}
	break;
    case T_CWD:				// change working directory
	if (opt_CRLF()) {
	    logcmd(t);
	    cwdCmd("/");
	    return (TRUE);
	} else if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    cwdCmd(s);
	    return (TRUE);
	}
	break;
    case T_CDUP:			// CWD ..
	logcmd(t);
	cwdCmd("..");
	return (TRUE);
    case T_PWD:				// print working directory
	if (CRLF()) {
	    logcmd(t);
	    pwdCmd();
	    return (TRUE);
	}
	break;
    case T_STAT:			// stat file/server status
	if (opt_CRLF()) {
	    logcmd(t);
	    statusCmd();
	    return (TRUE);
	} else if (checklogin(T_STAT) && pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    statFileCmd(s);
	    return (TRUE);
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
	    return (TRUE);
	}
	break;
    case T_CHMOD:			// set file protection
	if (SP() && STRING(s, "filename") && SP() && NUMBER(n)) {
	    chmodCmd(s, (u_int) n);
	    return (TRUE);
	}
	break;
    case T_MDTM:			// return file last modification time
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    mdtmCmd(s);
	    return (TRUE);
	}
	break;
    case T_DELE:			// delete file/document
	if (pathname_param(s)) {
	    logcmd(t, "%s", (const char*) s);
	    deleCmd(s);
	    return (TRUE);
	}
	break;
    case T_ABOR:			// abort active command/incoming call
	if (opt_CRLF()) {
	    logcmd(t);
	    ack(225, cmdToken(t));
	    return (TRUE);
	} else if (checkadmin(T_ABOR) && string_param(s, "modem")) {
	    abortCallCmd(s);
	    return (TRUE);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    logcmd(t);
	    helpCmd(cmdtab, (char*) NULL);
	    return (TRUE);
	} else if (string_param(s, "command name")) {
	    logcmd(t, "%s", (const char*) s);
	    s.raisecase();
	    if (s == "SITE")
		helpCmd(sitetab, NULL);
	    else if (s == "JPARM" || s == "JGPARM")
		helpCmd(parmtab, NULL);
	    else
		helpCmd(cmdtab, s);
	    return (TRUE);
	}
	break;
    case T_JNEW:			// create new job
	if (CRLF()) {
	    newJobCmd();
	    return (TRUE);
	}
	break;
    case T_JOB:				// select current job
	if (job_param(s)) {
	    setCurrentJob(s);
	    return (TRUE);
	}
	break;
    case T_JREST:			// reset job state
	if (job_param(s)) {
	    resetJob(s);
	    return (TRUE);
	}
	break;
    case T_JDELE:			// delete job
	if (job_param(s)) {
	    deleteJob(s);
	    return (TRUE);
	}
	break;
    case T_JINTR:			// interrupt job
	if (job_param(s)) {
	    interruptJob(s);
	    return (TRUE);
	}
	break;
    case T_JKILL:			// kill job
	if (job_param(s)) {
	    killJob(s);
	    return (TRUE);
	}
	break;
    case T_JSUSP:			// suspend job
	if (job_param(s)) {
	    suspendJob(s);
	    return (TRUE);
	}
	break;
    case T_JSUB:			// submit job
	if (job_param(s)) {
	    submitJob(s);
	    return (TRUE);
	}
	break;
    case T_JWAIT:			// wait for job
	if (job_param(s)) {
	    waitForJob(s);
	    return (TRUE);
	}
	break;
    case T_JGNEW:			// create job group
	if (CRLF()) {
	    curJob->groupid = curJob->jobid;
	    ack(200, cmdToken(t));
	    return (TRUE);
	}
	break;
    case T_JGREST:			// reset current job group state
	if (CRLF()) {
	    return (TRUE);
	}
	break;
    case T_JGRP:			// select current job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGDELE:			// delete job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGINTR:			// interrupt job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGKILL:			// kill job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGSUSP:			// suspend job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGSUB:			// submit job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_JGWAIT:			// wait for job group
	if (jgrp_param(s)) {
	    return (TRUE);
	}
	break;
    case T_NOOP:			// no-op
	if (CRLF()) {
	    logcmd(t);
	    ack(200, cmdToken(t));
	    return (TRUE);
	}
	break;
    case T_IDLE:			// set/query idle timeout
	if (opt_CRLF()) {
	    logcmd(t);
	    reply(213, "%u seconds.", idleTimeout);
	    return (TRUE);
	} else if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    if (n > maxIdleTimeout && !IS(PRIVILEGED)) {
		idleTimeout = maxIdleTimeout;
		reply(213, "%lu: Idle timeout too large, set to %u.",
		    n, maxIdleTimeout);
	    } else {
		idleTimeout = (int) n;
		reply(213, "Idle timeout set to %u.", idleTimeout);
	    }
	    return (TRUE);
	}
	break;
    case T_QUIT:			// terminate session
	if (CRLF()) {
	    logcmd(t);
	    reply(221, "Goodbye.");
	    dologout(0);
	    return (TRUE);
	}
	break;
    case T_TZONE:			// set/query timezone usage
	if (opt_CRLF()) {
	    reply(200, "Time values are handled in %s.", 
		IS(USEGMT) ? "GMT" : tzname[0]);
	    return (TRUE);
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
	    return (TRUE);
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
	    return (TRUE);
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
	    return (TRUE);
	}
	break;
    case T_JPARM:			// set/query job parameter
	if (opt_CRLF()) {
	    logcmd(T_JPARM);
	    if (checkJobState(curJob))
		jstatCmd(*curJob);
	    return (TRUE);
	} else if (SP() && getToken(T_STRING, "parameter name")) {
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
	if (SP() && getToken(T_STRING, "parameter name")) {
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
	if (SP() && getToken(T_STRING, "site command")) {
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
	if (checkadmin(T_ANSWER) && SP() && STRING(s, "modem")) {
	    fxStr how;
	    if (opt_CRLF())
		how = "any";
	    else if (!string_param(how, "answer-how"))
		break;
	    answerCallCmd(s, how);
	    return (TRUE);
	}
	break;
    case T_CHOWN:			// assign file ownership
	if (checkadmin(T_CHOWN) && SP() && STRING(s, "filename")) {
	    fxStr who;
	    if (string_param(who, "user")) {
		chownCmd(s, who);
		return (TRUE);
	    }
	}
	break;
    case T_DISABLE:			// disable outbound-usage of modem
	if (checkadmin(T_DISABLE) && SP() && STRING(s, "modem")) {
	    fxStr reason;
	    if (opt_CRLF())
		reason = "<unspecified reason>";
	    else if (!SP() || !multi_STRING(reason))
		break;
	    disableModemCmd(s, reason);
	    return (TRUE);
	}
	break;
    case T_ENABLE:			// enable outbound-usage of modem
	if (checkadmin(T_ENABLE) && string_param(s, "modem")) {
	    enableModemCmd(s);
	    return (TRUE);
	}
	break;
    case T_SHUT:			// shutdown server
	if (checkadmin(T_SHUT) && SP() && STRING(s, "shutdown-time")) {
	    fxStr reason;
	    if (opt_CRLF())
		reason = "<unspecified reason>";
	    else if (!SP() || !multi_STRING(reason))
		break;
	    const char* cp = s;
	    if (s.length() == 3 && strcasecmp(cp, "NOW") == 0) {
		time_t t = Sys::now();
		shutCmd(*localtime(&t), reason);
		return (TRUE);
	    } else if (s.length() == 4 && checkNUMBER(cp)) {
		time_t t = Sys::now();
		struct tm tm = *localtime(&t);
		tm.tm_hour = 60*60*twodigits(cp, 60);
		tm.tm_min  =    60*twodigits(cp+2, 60);
		shutCmd(tm, reason);
		return (TRUE);
	    } else if (s.length() == 12 && checkNUMBER(cp)) {
		struct tm tm;
		tm.tm_sec  = 0;
		tm.tm_min  = twodigits(cp+10, 60);
		tm.tm_hour = twodigits(cp+8, 24);
		tm.tm_mday = twodigits(cp+6, 31);
		tm.tm_mon  = twodigits(cp+4, 12) -1;
		tm.tm_year = fourdigits(cp+0) - 1900;
		shutCmd(tm, reason);
		return (TRUE);
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
	    return (TRUE);
	}
	break;
    }
    return (FALSE);
}

/*
 * Site-specific protocol commands (one line).
 */
fxBool
HylaFAXServer::site_cmd(Token t)
{
    fxStr s;

    switch (t) {
    case T_ADDUSER:
	if (checkadmin(T_ADDUSER) && SP() && STRING(s, "user-spec")) {
	    fxStr pass;
	    if (opt_CRLF()) {
		addUserCmd(s, "", "");
		return (TRUE);
	    } else if (SP() && STRING(pass, "password")) {
		fxStr apass;
		if (opt_CRLF()) {
		    addUserCmd(s, pass, "");
		    return (TRUE);
		} else if (string_param(apass, "admin-password")) {
		    addUserCmd(s, pass, apass);
		    return (TRUE);
		}
	    }
	}
	break;
    case T_DELUSER:
	if (checkadmin(T_DELUSER) && string_param(s, "user-spec")) {
	    delUserCmd(s);
	    return (TRUE);
	}
	break;
    case T_DELMODEM:
	if (checkadmin(T_DELMODEM) && string_param(s, "modem")) {
	    delModemCmd(s);
	    return (TRUE);
	}
	break;
    case T_ADDMODEM:
	if (checkadmin(T_ADDMODEM) && string_param(s, "modem")) {
	    addModemCmd(s);
	    return (TRUE);
	}
	break;
    case T_CONFIG:
	if (checkadmin(T_CONFIG) && SP() && STRING(s, "modem")) {
	    fxStr config;
	    if (opt_CRLF()) {
		configQueryCmd(s);
		return (TRUE);
	    } else if (SP() && multi_STRING(config)) {
		configCmd(s, config);
		return (TRUE);
	    }
	}
	break;
    case T_TRIGGER:
	if (string_param(s, "trigger-spec")) {
	    logcmd(t, s);
	    triggerCmd("%s", (const char*) s);
	    return (TRUE);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    helpCmd(sitetab, (char*) NULL);
	    return (TRUE);
	} else if (string_param(s, "command name")) {
	    logcmd(T_SITE, "HELP %s", (const char*) s);
	    s.raisecase();
	    helpCmd(sitetab, s);
	    return (TRUE);
	}
	break;
    }
    return (FALSE);
}

fxBool
HylaFAXServer::param_cmd(Token t)
{
    fxStr s;
    long n;
    time_t ticks;
    fxBool b;

    switch (t) {
    case T_SENDTIME:			// time to send job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (SP() && getToken(T_STRING, "time specification")) {
	    tokenBody.raisecase();
	    if (tokenBody == "NOW") {
		if (CRLF() && setJobParameter(*curJob, t, (time_t) 0)) {
		    reply(213, "%s set to NOW.", parmToken(t));
		    return (TRUE);
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
		    return (TRUE);
		}
	    }
	}
	break;
    case T_LASTTIME:			// time to kill job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (timespec_param(6, ticks) &&
	  setJobParameter(*curJob, t, ticks)) {
	    reply(213, "%s set to %02d%02d%02d."
		, parmToken(t)
		, ticks/(24*60*60)
		, (ticks/(60*60))%24
		, (ticks/60)%60
	    );
	    return (TRUE);
	}
	break;
    case T_RETRYTIME:			// retry interval for job
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (timespec_param(4, ticks) &&
	  setJobParameter(*curJob, t, ticks)) {
	    reply(213, "%s set to %02d%02d."
		, parmToken(t)
		, ticks/60
		, ticks%60
	    );
	    return (TRUE);
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
	    return (TRUE);
	} else if (number_param(n) &&
	  setJobParameter(*curJob, t, (u_short) n)) {
	    reply(213, "%s set to %u.", parmToken(t), n);
	    return (TRUE);
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
    case T_FROM_USER:
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
    case T_DONEOP:
    case T_COMMID:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (SP() && multi_STRING(s) && CRLF() &&
	  setJobParameter(*curJob, t, s)) {
	    reply(213, "%s set to \"%s\".", parmToken(t), (const char*) s);
	    return (TRUE);
	}
	break;
    case T_USE_ECM:
    case T_USE_TAGLINE:
    case T_USE_CONTCOVER:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (boolean_param(b) &&
	  setJobParameter(*curJob, t, b)) {
	    reply(213, "%s set to %s.", parmToken(t), b ? "YES" : "NO");
	    return (TRUE);
	}
	break;
    case T_CHOPTHRESH:
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (string_param(s, "floating point number") &&
	  setJobParameter(*curJob, t, (float) atof(s))) {
	    reply(213, "%s set to %g.", parmToken(t), (float) atof(s));
	    return (TRUE);
	}
	break;
     case T_COVER:			// specify/query cover page document
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (pathname_param(s)) {
	    addCoverDocument(*curJob, s);
	    return (TRUE);
	}
	break;
    case T_DOCUMENT:			// specify/query normal document
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (pathname_param(s)) {
	    addDocument(*curJob, s);
	    return (TRUE);
	}
	break;
    case T_POLL:			// specify/query poll operation
	if (opt_CRLF()) {
	    replyJobParamValue(*curJob, 213, t);
	    return (TRUE);
	} else if (SP() && STRING(s, "polling selector")) {
	    fxStr pwd;
	    if (opt_CRLF()) {
		addPollOp(*curJob, s, "");	// sep but no pwd
		return (TRUE);
	    } else if (SP() && pwd_param(pwd) && CRLF()) {
		addPollOp(*curJob, s, pwd);	// sep & pwd
		return (TRUE);
	    }
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    helpCmd(parmtab, (char*) NULL);
	    return (TRUE);
	} else if (string_param(s, "parameter name")) {
	    s.raisecase();
	    logcmd(T_JPARM, "HELP %s", (const char*) s);
	    helpCmd(parmtab, s);
	    return (TRUE);
	}
	break;
    }
    return (FALSE);
}

/*
 * Collect a single string parameter.
 */
fxBool
HylaFAXServer::string_param(fxStr& s, const char* what)
{
    return SP() && STRING(s, what) && CRLF();
}

/*
 * Collect a single number parameter.
 */
fxBool
HylaFAXServer::number_param(long& n)
{
    return SP() && NUMBER(n) && CRLF();
}

/*
 * Collect a single boolean parameter.
 */
fxBool
HylaFAXServer::boolean_param(fxBool& b)
{
    return SP() && BOOLEAN(b) && CRLF();
}

/*
 * Collect a single time parameter.
 */
fxBool
HylaFAXServer::timespec_param(int ndigits, time_t& t)
{
    return SP() && TIMESPEC(ndigits, t) && CRLF();
}

/*
 * File manipulation command.
 */
fxBool
HylaFAXServer::pathname_param(fxStr& s)
{
    return SP() && pathname(s) && CRLF();
}

/*
 * Job control commands.
 */
fxBool
HylaFAXServer::job_param(fxStr& jid)
{
    if (opt_CRLF()) {
	jid = curJob->jobid;
	return (TRUE);
    } else if (SP() && STRING(jid, "job identifer") && CRLF()) {
	jid.lowercase();
	return (TRUE);
    }
    return (FALSE);
}

/*
 * Job group control commands.
 */
fxBool
HylaFAXServer::jgrp_param(fxStr& jgid)
{
    if (opt_CRLF()) {
	jgid = curJob->groupid;
	return (TRUE);
    } else if (SP() && STRING(jgid, "job group identifier") && CRLF()) {
	jgid.lowercase();
	return (TRUE);
    }
    return (FALSE);
}

fxBool
HylaFAXServer::pwd_param(fxStr& s)
{
    return STRING(s, "polling password");
}

fxBool
HylaFAXServer::pathname(fxStr& s)
{
    return STRING(s, "pathname");
}

fxBool
HylaFAXServer::TIMESPEC(u_int len, time_t& result)
{
    if (getToken(T_STRING, "time specification")) {
	if (tokenBody.length() == len) {
	    if (checkNUMBER(tokenBody)) {
		const char* cp = tokenBody;
		if (len == 12) {		// YYYYMMDDHHSS
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
		    result = 24*60*60*twodigits(cp, 24)
			   +    60*60*twodigits(cp+2, 60)
			   +       60*twodigits(cp+4, 60);
		} else {			// MMSS
		    result = 60*twodigits(cp, 60)
			   +    twodigits(cp+2, 60);
		}
		return (TRUE);
	    }
	} else
	    syntaxError(fxStr::format(
		"bad time specification (expecting %d digits)", len));
    }
    return (FALSE);
}

fxBool
HylaFAXServer::BOOLEAN(fxBool& b)
{
    if (getToken(T_STRING, "boolean")) {
	tokenBody.raisecase();
	if (tokenBody == "YES") {
	    b = TRUE;
	    return (TRUE);
	} else if (tokenBody == "NO") {
	    b = FALSE;
	    return (TRUE);
	} else
	    syntaxError("invalid boolean value, use YES or NO");
    }
    return (FALSE);
}

fxBool
HylaFAXServer::checkToken(Token wanted)
{
    Token t = nextToken();
    if (t != wanted) {
	pushToken(t);
	return (FALSE);
    } else
	return (TRUE);
}

fxBool
HylaFAXServer::getToken(Token wanted, const char* expected)
{
    Token t = nextToken();
    if (t != wanted) {
	if (t == T_LEXERR)
	    syntaxError("unmatched quote mark");
	else
	    syntaxError(fxStr::format("expecting %s", expected));
	return (FALSE);
    } else
	return (TRUE);
}

fxBool HylaFAXServer::SP()		{ return getToken(T_SP, "<SP>"); }
fxBool HylaFAXServer::COMMA()		{ return getToken(T_COMMA, "\",\""); }
fxBool HylaFAXServer::CRLF()		{ return getToken(T_CRLF, "<CRLF>"); }
fxBool HylaFAXServer::opt_CRLF()	{ return checkToken(T_CRLF); }

fxBool
HylaFAXServer::opt_STRING(fxStr& s)
{
    if (checkToken(T_STRING)) {
	s = tokenBody;
	return (TRUE);
    } else
	return (FALSE);
}
fxBool
HylaFAXServer::STRING(fxStr& s, const char* what)
{
    if (getToken(T_STRING, what != NULL ? what : "<STRING>")) {
	s = tokenBody;
	return (TRUE);
    } else
	return (FALSE);
}
fxBool
HylaFAXServer::multi_STRING(fxStr& s)
{
    if (!STRING(s))
	return (FALSE);
    for (;;) {
	switch (nextToken()) {
	case T_CRLF:
	    pushToken(T_CRLF);
	    return (TRUE);
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
	    return (FALSE);
	}
    }
}

fxBool
HylaFAXServer::checkNUMBER(const char* cp)
{
    if (cp[0] == '-' || cp[0] == '+')
	cp++;
    for (; *cp; cp++)
	if (!isdigit(*cp)) {
	    syntaxError("invalid number");
	    return (FALSE);
	}
    return (TRUE);
}

fxBool
HylaFAXServer::NUMBER(long& n)
{
    if (!getToken(T_STRING, "decimal number")) {
	return (FALSE);
    } else if (!checkNUMBER(tokenBody)) {
	return (FALSE);
    } else {
	n = strtol(tokenBody, NULL, 0);
	return (TRUE);
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
HylaFAXServer::getChar(fxBool waitForInput)
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
		dologout(1);		// XXX what about server interactions???
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
    if (recvNext + n > sizeof (recvBuf)) {
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
fxBool
HylaFAXServer::getCmdLine(char* s, int n, fxBool waitForInput)
{
    char* cp = s;
    cpos = 0;
    while (n > 1) {
	int c = getChar(cp != s || waitForInput);
	if (c == EOF)
	    return (FALSE);
	if (c == IAC) {			// telnet protocol command
	    c = getChar(TRUE);
	    if (c == EOF)
		return (FALSE);
	    switch (c&0377) {
	    case WILL:
	    case WONT:
		c = getChar(TRUE);	// telnet option
		if (c == EOF)
		    return (FALSE);
		printf("%c%c%c", IAC, DONT, c&0377);
		(void) fflush(stdout);
		continue;
	    case DO:
	    case DONT:
		c = getChar(TRUE);	// telnet option
		if (c == EOF)
		    return (FALSE);
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
    return (TRUE);
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

fxBool
HylaFAXServer::checklogin(Token t)
{
    if (!IS(LOGGEDIN)) {
	cmdFailure(t, "not logged in");
	reply(530, "Please login with USER and PASS.");
	return (FALSE);
    } else
	return (TRUE);
}

fxBool
HylaFAXServer::checkadmin(Token t)
{
    if (checklogin(t)) {
	if (IS(PRIVILEGED))
	    return (TRUE);
	cmdFailure(t, "user is not privileged");
	reply(530, "Please use ADMIN to establish administrative privileges.");
    }
    return (FALSE);
}
