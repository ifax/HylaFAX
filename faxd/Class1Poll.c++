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
#include "Class1.h"
#include "ModemConfig.h"
#include "HDLCFrame.h"
#include "t.30.h"

fxBool
Class1Modem::requestToPoll(fxStr&)
{
    return TRUE;
}

fxBool
Class1Modem::pollBegin(const fxStr& cig0,
    const fxStr& sep0, const fxStr& pwd0, fxStr& emsg)
{
    u_int dtc = (modemDIS() &~ DIS_XTNDFIELD);
    u_int dtc_xinfo = 0;
    u_int send = 0;
    fxStr cig;
    encodeTSI(cig, cig0);
    fxStr sep;
    if (sep0 != fxStr::null && (xinfo&DIS_SEP)) {
	encodePWD(sep, sep0);
	send |= DIS_SEP;
    }
    fxStr pwd;
    if (pwd0 != fxStr::null && (xinfo&DIS_PWD)) {
	encodePWD(pwd, pwd0);
	send |= DIS_PWD;
    }

    setInputBuffering(FALSE);
    prevPage = FALSE;				// no previous page received
    pageGood = FALSE;				// quality of received page

    return atCmd(thCmd, AT_NOTHING) &&
	atResponse(rbuf, 2550) == AT_CONNECT &&
	recvIdentification(
	    (send&DIS_PWD ? FCF_PPW : 0), pwd,
	    (send&DIS_SEP ? FCF_SEP : 0), sep,
	    FCF_CIG, cig,
	    FCF_DTC, dtc, dtc_xinfo,
	    conf.t1Timer, emsg);
}
