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
 * Administrative Support.
 */
#include "HylaFAXServer.h"
#include "Sys.h"
#include "config.h"

#include <time.h>

/*
 * Abort an active call on the specified modem.
 * The best we can do is send a message to the
 * current process via the FIFO for that modem;
 * we cannot be certain if the abort operation
 * actually takes place.
 */
void
HylaFAXServer::abortCallCmd(const char* modem)
{
    logcmd(T_ABOR, "%s", modem);
    fxStr emsg;
    if (sendModem(modem, emsg, "Z"))
	reply(200, "Modem %s told to abort current call.", modem);
    else
	reply(500, "Abort request failed: %s.", (const char*) emsg);
}

/*
 * Answer an inbound call on the specified modem.
 * The best we can do is send a message to the
 * faxgetty process via the FIFO for that modem;
 * we cannot be certain if the operation happens
 * or is successful because there is no return
 * channel.
 *
 * The ``how'' parameter should be one of fax,
 * data, voice, or any (note all lower case).
 */
void
HylaFAXServer::answerCallCmd(const char* modem, const char* how)
{
    logcmd(T_ANSWER, "%s %s", modem, how);
    fxStr emsg;
    if (sendModem(modem, emsg, "A%s", how))
	reply(200, "Modem %s told to answer call as %s.", modem, how);
    else
	reply(500, "Answer request failed: %s.", (const char*) emsg);
}

/*
 * Disable outbound use of the specified modem.  We
 * first try to notify the appropriate faxgetty process
 * and if that fails then go directly to the scheduler.
 * This should handle both send+recv and send-only system
 * configurations.
 *
 * NB: There currently is no place to stash the reason
 *     the modem's use is disabled.
 */
void
HylaFAXServer::disableModemCmd(const char* modem, const char* reason)
{
    logcmd(T_DISABLE, "%s %s", modem, reason);
    fxStr emsg;
    if (sendModem(modem, emsg, "SD"))
	reply(200, "Modem %s told to disable outbound use.", modem);
    else if (sendQueuer(emsg, "+%s:D", modem))
	reply(200, "Scheduler told to disable use of modem %s.", modem);
    else
	reply(500, "Unable to disable use of %s: %s.",
	    modem, (const char*) emsg);
}

/*
 * Enable outbound use of the specified modem.  We
 * first try to notify the appropriate faxgetty process
 * and if that fails then go directly to the scheduler.
 * This should handle both send+recv and send-only system
 * configurations.
 */
void
HylaFAXServer::enableModemCmd(const char* modem)
{
    logcmd(T_ENABLE, "%s", modem);
    fxStr emsg;
    if (sendModem(modem, emsg, "SR"))
	reply(200, "Modem %s told to enable outbound use.", modem);
    else if (sendQueuer(emsg, "+%s:R", modem))
	reply(200, "Scheduler told to enable use of modem %s.", modem);
    else
	reply(500, "Unable to enable use of %s: %s.",
	    modem, (const char*) emsg);
}

/*
 * Shutdown client access to the server machine by installing
 * a shutdown file with the specified reason.  The shutdown
 * may be scheduled for some time in the future in which case
 * when indicates when the shutdown is to take place.
 */
void
HylaFAXServer::shutCmd(const struct tm& when, const char* reason)
{
    logcmd(T_SHUT, "%.24s %s", asctime(&when), reason);
    if (shutdownFile == "") {
	reply(503, "Null configured shutdown filename; something is hosed.");
	return;
    }
    const char* msg = "Shutdown failed; ";
    char templ[128];
    sprintf(templ, "/%s/shutXXXXXX", FAX_TMPDIR);
    int fd = Sys::mkstemp(templ);
    if (fd < 0) {
	reply(550, "%serror creating temp file %s: %s.", msg,
	    (const char*) templ, strerror(errno));
	return;
    }
    FILE* fp = fdopen(fd, "w");
    if (fp) {
	fprintf(fp, "%d %d %d %d %d 5 1\n"
	    , when.tm_year+1900
	    , when.tm_mon
	    , when.tm_mday
	    , when.tm_hour
	    , when.tm_min
	);
	fprintf(fp, "\n%s\n\n", reason);
	if (fclose(fp) != 0) {
	    reply(450, "%sI/O error writing %s.", msg, (const char*) templ);
	    (void) Sys::unlink(templ);
	} else if (Sys::rename(templ, fixPathname(shutdownFile)) < 0) {
	    reply(550, "%srename %s.", msg, strerror(errno));
	} else {
	    reply(200, "System shutdown scheduled for %.24s.",
		asctime(&when));
	    return;
	}
    } else {
	reply(550, "%serror opening file: %s", msg, strerror(errno));
	Sys::close(fd);
    }
    (void) Sys::unlink(templ);
}

void
HylaFAXServer::addModemCmd(const char* modem)
{
    logcmd(T_ADDMODEM, "%s", modem);
    reply(200, "Modem %s added.", modem);		// XXX
}

void
HylaFAXServer::delModemCmd(const char* modem)
{
    logcmd(T_DELMODEM, "%s", modem);
    reply(200, "Modem %s deleted.", modem);		// XXX
}

void
HylaFAXServer::configQueryCmd(const char* where)
{
    logcmd(T_CONFIG, "%s", where);
    lreply(200, "Configuration info for %s.", where);	// XXX
    // XXX
    reply(200, "End of configuration info.");
}

void
HylaFAXServer::configCmd(const char* where, const char* info)
{
    logcmd(T_CONFIG, "%s %s", where, info);
    reply(200, "Config info %s sent to %s.", info, where);	// XXX
}
