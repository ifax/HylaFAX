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
#ifndef _FaxItem_
#define	_FaxItem_
/*
 * HylaFAX request substructure.
 */
#include "Str.h"
#include "Array.h"

typedef unsigned short FaxSendOp;

/*
 * The FaxRequest class contains an array of
 * these structures.  This should really be a
 * nested class but due to compiler limitations
 * we are forced to make it public.
 */
struct FaxItem {
    FaxSendOp	op;		// send operation type
    u_short	dirnum;		// directory index for TIFF images
    fxStr	item;		// filename/password for transmit/poll
    fxStr	addr;		// SUB/SEP for transmit/poll

    FaxItem();
    FaxItem(FaxSendOp, u_short dirnum, const fxStr& addr, const fxStr& item);
    FaxItem(const FaxItem& other);
    ~FaxItem();

    int compare(FaxItem const *a) const;	// XXX needed for array

    bool isSavedOp() const;
};
fxDECLARE_ObjArray(FaxItemArray, FaxItem)
#endif /* _FaxItem_ */
