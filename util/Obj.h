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
#ifndef _Obj_
#define	_Obj_

#include "Types.h"

// Reference counted objects.  Subclasses of this can use Ptr's.

class fxObj {
public:
    fxObj();
    fxObj(const fxObj& other);
    virtual ~fxObj();
// Memory management
    void inc();
    void dec();
    u_long getReferenceCount();

// Misc
    virtual const char* className() const;
    int compare(const fxObj *) const;
    virtual void subClassMustDefine(const char* method) const;
protected:
    u_long	referenceCount;
};

inline void fxObj::inc()			{ ++referenceCount; }
inline void fxObj::dec() {
    fxAssert(referenceCount>0,"Bogus object reference count");
    if (0 >= --referenceCount) delete this;
}
inline u_long fxObj::getReferenceCount()	{ return referenceCount; }
#endif /* _Obj_ */
