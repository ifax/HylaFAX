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
#ifndef _FaxRecvInfo_
#define	_FaxRecvInfo_
/*
 * Wrapper class for passing around information
 * about a receive operation.
 */
#include "Str.h"
#include "Class2Params.h"
#include "CallID.h"

class FaxRecvInfo : public fxObj {
public:
    fxStr	qfile;		// file containing data
    u_short	npages;		// total number of pages/page number
    fxStr	commid;		// communication identifier
    fxStr	sender;		// sender's TSI
    fxStr	passwd;		// sender's PWD
    fxStr	subaddr;	// subaddressing information
    u_int	time;		// time on the phone
    Class2Params params;	// transfer parameters
    fxStr	reason;		// reason for failure (if not null)
    CallID	callid;		// call identification

    FaxRecvInfo();
    FaxRecvInfo(const FaxRecvInfo& other);
    ~FaxRecvInfo();

    fxStr encode();
    bool decode(const char*);
};
#endif /* _FaxRecvInfo_ */
