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
#include "FaxAcctInfo.h"
#include "StackBuffer.h"
#include "Sys.h"
#include <sys/file.h>

#include "config.h"

extern	const char* fmtTime(time_t);

/*
 * Record an activity in the transfer log file.
 */
bool
FaxAcctInfo::record(const char* cmd)
{
    bool ok = false;
    int fd = Sys::open(FAX_XFERLOG, O_RDWR|O_CREAT|O_APPEND, 0644);
    if (fd >= 0) {
	fxStackBuffer record;
	char buf[80];
	strftime(buf, sizeof (buf), "%D %H:%M", localtime(&start));
	record.put(buf);			// $ 1 = time
	record.fput("\t%s", cmd);		// $ 2 = SEND|RECV|POLL|PAGE
	record.fput("\t%s", commid);		// $ 3 = commid
	record.fput("\t%s", device);		// $ 4 = device
	record.fput("\t%s", jobid);		// $ 5 = jobid
	u_int i = 0;
	char c;
	for (const char* cp = jobtag; (c = *cp); cp++) {
	    if (i == sizeof (buf)-2)		// truncate string
		break;
	    if (c == '\t')			// tabs are field delimiters
		c = ' ';
	    else if (c == '"')			// escape quote marks
		buf[i++] = '\\';
	    buf[i++] = c;
	}
	buf[i] = '\0';
	record.fput("\t\"%s\"", buf);		// $ 6 = jobtag
	record.fput("\t%s", user);		// $ 7 = sender
	record.fput("\t\"%s\"", dest);		// $ 8 = dest
	record.fput("\t\"%s\"", csi);		// $ 9 = csi
	record.fput("\t%u", params);		// $10 = encoded params and DCS
	record.fput("\t%d", npages);		// $11 = npages
	record.fput("\t%s", fmtTime(duration));	// $12 = duration
	record.fput("\t%s", fmtTime(conntime));	// $13 = conntime
	record.fput("\t\"%s\"", status);	// $14 = status
	record.fput("\t\"%s\"", callid.size() > CallID::NAME ? (const char*) callid[1] : "");	// $15 = CallID2/CIDName
	record.fput("\t\"%s\"", callid.size() > CallID::NUMBER ? (const char*) callid[0] : "");	// $16 = CallID1/CIDNumber
	fxStr callid_formatted = "";
	for (int i = 2; i < callid.size(); i++) {
	    if (i > 2) callid_formatted.append("::");
	    callid_formatted.append(callid[i]);
	}
	record.fput("\t\"%s\"", (const char*) callid_formatted);	// $17 = CallID3 -> CallIDn
	record.fput("\t\"%s\"", owner);					// $18 = owner
	record.fput("\t\"%s\"", (const char*) faxdcs);			// $19 = DCS
	record.put('\n');
	flock(fd, LOCK_EX);
	ok = (Sys::write(fd, record, record.getLength()) == (ssize_t)record.getLength());
	Sys::close(fd);				// implicit unlock
    }
    return (ok);
}
