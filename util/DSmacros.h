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
#ifndef _DSmacros_
#define	_DSmacros_
#include "Types.h"

// ----------------- copy macros ---------------------------------------------
#define fxIMPLEMENT_copyStruct(CLASS,FIELD,TYPE)			\
void CLASS::fxCAT(copy,FIELD)(void const *s, void *d) const		\
{   *((TYPE *)d) = *(TYPE const *)s; }					\
__enddef__

#define fxIMPLEMENT_copyObj(CLASS,FIELD,TYPE)				\
void CLASS::fxCAT(copy,FIELD)(void const *s, void *d) const		\
{   new(d) TYPE(*(TYPE const *)s); }					\
__enddef__

#define fxIMPLEMENT_copyPtr(CLASS,FIELD,TYPE)				\
	fxIMPLEMENT_copyStruct(CLASS,FIELD,TYPE)			\
__enddef__
#define fxIMPLEMENT_copyObjPtr(CLASS,FIELD,TYPE)			\
	fxIMPLEMENT_copyObj(CLASS,FIELD,TYPE)				\
__enddef__

// ----------------- create macros -------------------------------------------
#define fxIMPLEMENT_createObj(CLASS,FIELD,TYPE)				\
void CLASS::fxCAT(create,FIELD)(void *d) const { new(d) TYPE; }		\
__enddef__

#define fxIMPLEMENT_createPtr(CLASS,FIELD,TYPE)				\
void CLASS::fxCAT(create,FIELD)(void *d) const {*(TYPE *)d = 0;}	\
__enddef__

#define fxIMPLEMENT_createObjPtr(CLASS,FIELD,TYPE)			\
	fxIMPLEMENT_createObj(CLASS,FIELD,TYPE)				\
__enddef__

#define fxIMPLEMENT_createStruct(CLASS,FIELD,TYPE)			\
void CLASS::fxCAT(create,FIELD)(void *) const {}			\
__enddef__

// ----------------- destroy macros ------------------------------------------
#define fxIMPLEMENT_destroyStruct(CLASS,FIELD,TYPE)			\
void CLASS::fxCAT(destroy,FIELD)(void *) const {}			\
__enddef__

#define fxIMPLEMENT_destroyObj(CLASS,FIELD,TYPE)			\
void CLASS::fxCAT(destroy,FIELD)(void *d) const				\
{   ((TYPE *)d)->TYPE::~TYPE();	}					\
__enddef__

#define fxIMPLEMENT_destroyPtr(CLASS,FIELD,TYPE)			\
    fxIMPLEMENT_destroyStruct(CLASS,FIELD,TYPE)				\
__enddef__

#define fxIMPLEMENT_destroyObjPtr(CLASS,FIELD,TYPE)			\
    fxIMPLEMENT_destroyObj(CLASS,FIELD,TYPE)				\
__enddef__
#endif /* _DSmacros_ */
