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
#include "FaxRecvInfo.h"

FaxRecvInfo::FaxRecvInfo()
{
    npages = 0;
    time = 0;
}
FaxRecvInfo::FaxRecvInfo(const FaxRecvInfo& other)
    : fxObj(other)
    , qfile(other.qfile)
    , commid(other.commid)
    , sender(other.sender)
    , subaddr(other.subaddr)
    , params(other.params)
    , reason(other.reason)
{
    npages = other.npages;
    time = other.time;
}
FaxRecvInfo::~FaxRecvInfo() {}

fxStr
FaxRecvInfo::encode() const
{
    return fxStr::format("%x,%x,%x,%s,%s,\"%s\",\"%s\",\"%s\""
	, time
	, npages
	, params.encode()
	, (const char*) qfile
	, (const char*) commid
	, (const char*) sender
	, (const char*) subaddr
	, (const char*) reason
    );
}

bool
FaxRecvInfo::decode(const char* cp)
{
    char* np;
    time = (u_int) strtoul(cp, &np, 16);
    if (np == cp)
	return (false);
    npages = (u_short) strtoul(cp = np+1, &np, 16);
    if (np == cp)
	return (false);
    params.decode((u_int) strtoul(cp = np+1, &np, 16));
    if (np == cp)
	return (false);
    qfile = np+1;
    qfile.resize(qfile.next(0,','));
    cp = strchr(np+1, ',');
    if (cp == NULL)
	return (false);
    commid = cp+1;
    commid.resize(commid.next(0,','));
    cp = strchr(cp+1, '"');
    if (cp == NULL)
	return (false);
    sender = cp+1;
    sender.resize(sender.next(0,'"'));
    cp = strchr(cp+1, '"');
    if (cp == NULL || cp[1] != ',' || cp[2] != '"')
	return (false);
    subaddr = cp+1;
    subaddr.resize(subaddr.next(0,'"'));
    cp = strchr(cp+1, '"');
    if (cp == NULL || cp[1] != ',' || cp[2] != '"')
	return (false);
    reason = cp+3;			// +1 for "/+1 for ,/+1 for "
    reason.resize(reason.next(0,'"'));
    return (true);
}
