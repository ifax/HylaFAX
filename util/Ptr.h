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
#ifndef _Ptr_
#define	_Ptr_

#include "Obj.h"

/******************************
  What the declaration of a fxPtr class looks like to the user:

class fxPtr<T> : public fxPtr<> {
    fxPtr<T>();
    fxPtr<T>(<T>* obj);
    fxPtr<T>(const fxPtr<T>& other);
    ~fxPtr<T>();

    fxPtr<T>& operator=(const fxPtr<T>& other);
    fxPtr<T>& operator=(<T>* obj);
    operator <T>*();
    <T>* operator->();
};
******************************/

#define fxDECLARE_Ptr(TYPE)						\
class fxCAT(TYPE,Ptr) {/*XXX*/						\
protected:								\
    void destroy() { if (p) p->dec(); }					\
    TYPE* p;								\
public:							 		\
    fxCAT(TYPE,Ptr)() { p = 0; }					\
    fxCAT(TYPE,Ptr)(TYPE *tp) { p = tp ? (tp->inc(),tp) : 0; }		\
    fxCAT(TYPE,Ptr)(const fxCAT(TYPE,Ptr)& other)			\
	{ p = other.p ? (other.p->inc(),other.p) : 0; }			\
    ~fxCAT(TYPE,Ptr)() { destroy(); }					\
    fxCAT(TYPE,Ptr)& operator=(const fxCAT(TYPE,Ptr)& other) {		\
	if (p != other.p) {						\
	    destroy(); p = other.p ? (other.p->inc(),other.p) : 0;	\
	}								\
	return *this;							\
    }									\
    fxCAT(TYPE,Ptr)& operator=(TYPE* tp) {				\
	if (p != tp) {							\
	    destroy(); p = tp ? (tp->inc(),tp) : 0;			\
	}								\
	return *this;							\
    }									\
    int compare(const fxCAT(TYPE,Ptr) *other) const			\
	{ return int((char *)p - (char *)other->p); }			\
    operator TYPE*() { return p; }					\
    operator const TYPE*() const { return p; }				\
    TYPE* operator ->() { return p; }					\
    const TYPE* operator ->() const { return p; }			\
}									\
__enddef__
#endif /* _Ptr_ */
