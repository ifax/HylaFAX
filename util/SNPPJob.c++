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
#include "Sys.h"

#include <ctype.h>
#include <string.h>
#include <errno.h>

#if CONFIG_INETTRANSPORT
extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>            // XXX
}
#endif

#include "SNPPClient.h"
#include "FaxConfig.h"

SNPPJob::SNPPJob() {}
SNPPJob::SNPPJob(const SNPPJob& other)
    : mailbox(other.mailbox)
    , pin(other.pin)
    , passwd(other.passwd)
    , subject(other.subject)
{
    queued = other.queued;
    notify = other.notify;
    holdTime = other.holdTime;
    retryTime = other.retryTime;
    maxRetries = other.maxRetries;
    maxDials = other.maxDials;
    serviceLevel = other.serviceLevel;
}
SNPPJob::~SNPPJob() {}

#define	valeq(a,b)	(strcasecmp(a,b)==0)
#define	valneq(a,b,n)	(strncasecmp(a,b,n)==0)

bool
SNPPJob::setNotification(const char* v0)
{
    const char* v = v0;
    if (strneq(v, "when", 4)) {
	for (v += 4; isspace(*v); v++)
	    ;
    }
    if (valeq(v, "done"))
	notify = when_done;
    else if (valneq(v, "req", 3))
	notify = when_requeued;
    else if (valeq(v, "none") || valeq(v, "off"))
	notify = no_notice;
    else if (valeq(v, "default"))
	setNotification(SNPP_DEFNOTIFY);
    else
	return (false);
    return (true);
}
void SNPPJob::setNotification(PageNotify n)		{ notify = n; }
/*
 * Create the mail address for a local user.
 */
void
SNPPJob::setMailbox(const char* user)
{
    fxStr acct(user);
    if (acct != "" && acct.next(0, "@!") == acct.length()) {
	static fxStr domainName;
	if (domainName == "") {
	    char hostname[64];
	    (void) gethostname(hostname, sizeof (hostname));
#if CONFIG_INETTRANSPORT
	    struct hostent* hp = gethostbyname(hostname);
	    domainName = (hp ? hp->h_name : hostname);
#else
	    domainName = hostname;
#endif
	}
	mailbox = acct | "@" | domainName;
    } else
	mailbox = acct;
    // strip leading & trailing white space
    mailbox.remove(0, mailbox.skip(0, " \t"));
    mailbox.resize(mailbox.skipR(mailbox.length(), " \t"));
}

u_int
SNPPJob::parseTime(const char* v)
{
    char* cp;
    u_int t = (u_int) strtoul(v, &cp, 10);
    if (cp) {
	while (isspace(*cp))
	    ;
	if (strncasecmp(cp, "min", 3) == 0)
	    t *= 60;
	else if (strncasecmp(cp, "hour", 4) == 0)
	    t *= 60*60;
	else if (strncasecmp(cp, "day", 3) == 0)
	    t *= 24*60*60;
    }
    return (t);
}
void SNPPJob::setRetryTime(const char* v)		{ retryTime = parseTime(v); }
void SNPPJob::setRetryTime(u_int v)			{ retryTime = v; }

extern int
parseAtSyntax(const char* s, const struct tm& ref, struct tm& at0, fxStr& emsg);

bool
SNPPJob::setHoldTime(const char* s, fxStr& emsg)
{
    struct tm tts;
    time_t now = Sys::now();
    if (!parseAtSyntax(s, *localtime(&now), tts, emsg)) {
	emsg.insert(fxStr::format("%s: ", s));
	return (false);
    } else {
	setHoldTime((u_int) mktime(&tts));
	return (true);
    }
}
void SNPPJob::setHoldTime(u_int v)			{ holdTime = v; }
void SNPPJob::setMaxTries(u_int n)			{ maxRetries = n; }
void SNPPJob::setMaxDials(u_int n)			{ maxDials = n; }
void SNPPJob::setPIN(const char* s)			{ pin = s; }
void SNPPJob::setPassword(const char* s)		{ passwd = s; }
void SNPPJob::setQueued(bool v)			{ queued = v; }
void SNPPJob::setSubject(const char* s)			{ subject = s; }
void SNPPJob::setServiceLevel(u_int n)			{ serviceLevel = n; }

#define	CHECK(x)	{ if (!(x)) goto failure; }
#define	CHECKCMD(x)	CHECK(client.command(x) == SNPPClient::COMPLETE)
#define	CHECKCMD2(x,y)	CHECK(client.command(x,y) == SNPPClient::COMPLETE)
#define	CHECKPARM(a,b)	CHECK(client.siteParm(a,b))
#define	IFPARM(a,b,v)	{ if ((b) != (v)) CHECKPARM(a,b) }

bool
SNPPJob::createJob(SNPPClient& client, fxStr& emsg)
{
    if (holdTime != 0 && !client.setHoldTime((u_int) holdTime))
	goto failure;
    if (subject != "")
	CHECKCMD2("SUBJ %s", (const char*) subject)
    CHECKCMD2("LEVE %u", serviceLevel)
    if (client.hasSiteCmd()) {
	CHECKPARM("FROMUSER", client.getSenderName())
	if (retryTime != (u_int) -1 && !client.setRetryTime(retryTime))
	    goto failure;
	IFPARM("MODEM",	client.getModem(), "");	// XXX should be per-job state
	IFPARM("MAXDIALS",	maxDials,	   (u_int) -1)
	IFPARM("MAXTRIES",	maxRetries,	   (u_int) -1)
	CHECKPARM("MAILADDR",	mailbox)
	CHECKPARM("NOTIFY",
	    notify == when_done	?     "done" :
	    notify == when_requeued ? "done+requeue" :
				      "none")
	CHECKPARM("JQUEUE", queued ? "yes" : "no");
    }
    return (client.newPage(pin, passwd, jobid, emsg));
failure:
    emsg = client.getLastResponse();
    return (false);
}
#undef IFPARM
#undef CHECKPARM
#undef CHECKCMD
#undef CHECK

fxIMPLEMENT_ObjArray(SNPPJobArray, SNPPJob)
