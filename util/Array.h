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
#ifndef _Array_
#define _Array_
#include "Obj.h"
#include "Ptr.h"

// Here's what the declaration of an array class looks like to the user:
/*
class ARRAY<ITEM> : public fxArray {
public:
    ARRAY();
    ARRAY(u_int size);
    ARRAY(ARRAY const &);
    void operator=(ARRAY const &);
    ITEM & operator[](u_int index) const;
    u_int length() const;
    void append(ITEM const & item);
    void append(ARRAY const & a);
    void remove(u_int start, u_int leng=1);
    ARRAY cut(u_int start, u_int leng=1);
    void insert(ITEM const & item, u_int p);
    void insert(ARRAY const & a, u_int posn);
    void resize(u_int length);
    ARRAY extract(u_int start, u_int len);
    void head(u_int len);
    void tail(u_int len);
    u_int find(ITEM const & item) const;
    void qsort(u_int start, u_int len);
    void swap(u_int,u_int);

protected:
    virtual void getmem();
    virtual void expand();
    virtual void createElements(void*, u_int numbytes);
    virtual void destroyElements(void*, u_int numbytes);
    virtual void copyElements(void const *src, void *dst, u_int numbytes);
    virtual int compareElements(void const *elem1, void const *elem2);
};
*/

//
// There are three flavors of arrays:
//   struct Arrays (in which the contents of the elements are not looked at)
//   pointer Arrays (the elements are pointers to some memory)
//   object Arrays (the elements are objects, which must be constructed and
//	destructed)
//
// Macros exist for each of these fxArray flavors:
//   fxDECLARE_Array (same as fxDECLARE_StructArray)
//   fxDECLARE_PtrArray
//      (acts like fxDECLARE_Array, except that pointers are
//       initialized to nil when new elements are added)
//   fxDECLARE_ObjArray

static const u_int fx_invalidArrayIndex = (u_int) -1;

class fxArray : public fxObj {
public:
    u_int length() const;
    u_int elementSize() const
	{ return elementsize; }
    void resize(u_int length);
    void setMaxLength(u_int maxlength);
    void qsort(u_int posn, u_int len);
    void qsort();

    void swap(u_int,u_int);

    virtual char const *className() const = 0;

protected:
    class fxAddress {
    public:
	fxAddress()					{ ptr = 0; }
	fxAddress(void* p)				{ ptr = (char*) p; }
	fxAddress operator+(u_long offset) const	{ return ptr + offset; }
	bool operator==(const fxAddress& r) const	{ return ptr == r.ptr; }
	bool operator!=(const fxAddress& r) const	{ return ptr != r.ptr; }
	// NB: operator const void*() const does not work
	operator void*() const				{ return ptr; }
    protected:
	char* ptr;
    };

    fxArray(u_short esize, u_int initlength=0);
    fxArray(u_int esize, u_int num, void *data);
    fxArray(fxArray const &);
    virtual ~fxArray();

    void * operator[](u_int index) { return data + elementsize*index; }
    void operator=(fxArray const &);

    void append(void const *item);
    void append(fxArray const &);
    void remove(u_int start, u_int length=1);
    void insert(fxArray const &, u_int posn);
    void insert(void const *item, u_int posn);

    u_int find(void const *, u_int start=0) const;

	// The objects in the array are stored sequentially at the
	// location pointed to by data. The length of the known
	// allocated segment is stored in maxi, in bytes. The
	// length of the array is stored in num, in *bytes*. The
	// size of an array element is stored in elementsize, in bytes.

	// data is allowed to be nil iff (maxi==0)
    fxAddress data;

	// num <= maxi
    u_int maxi,num;
    u_short elementsize;

	// These two methods control how the array class goes to
	// fetch more memory.
    virtual void getmem();
    virtual void expand();

	// The raw methods are used to
	// implement methods which return an fxArray type.
    void * raw_copy() const;
    void * raw_extract(u_int start, u_int length) const;
    void * raw_cut(u_int start, u_int length);
    void * raw_head(u_int) const;
    void * raw_tail(u_int) const;

    void qsortInternal(u_int, u_int, void *);
    void destroy();

	// These three methods can be overridden to properly copy, delete,
	// and create new array elements in the desired manner. By default
	// `create' and `destroy' do nothing, and `copy' is a simple bcopy.
	//
	// The job of create is to take an area of uninitialized memory and
	// create a series of valid objects in it. The job of destroy is to
	// take a series of valid objects and destroy any resources they
	// consume. The status of the memory after the destroy is irrelevant.
	// The job of copy is to take a source array of objects, and copy
	// them to an area of *uninitialized* memory. There will not be any
	// objects stored there previous to the copy.
    virtual void createElements(void *, u_int numbytes);
    virtual void destroyElements(void *, u_int numbytes);
    virtual void copyElements(void const *src, void *dst, u_int numbytes)
	const;
    virtual int compareElements(void const *, void const *) const;
};

#define fxArrayHeader(ARRAY,ITEM)					\
    ARRAY();								\
    ARRAY(u_int size);							\
    ARRAY(ARRAY const&a);						\
    ~ARRAY();								\
    virtual const char* className() const;				\
    void operator=(ARRAY const&a) {					\
	maxi = a.maxi; num = a.num; if (data) delete (void*)data;	\
	data = a.raw_copy(); }						\
    ITEM & operator[](u_int index) {					\
      fxAssert(index*sizeof(ITEM) < num, "Invalid Array[] index");	\
      return *(ITEM *)((char *)((void *)data) + index*sizeof(ITEM));	\
    }									\
    const ITEM & operator[](u_int index) const {			\
      fxAssert(index*sizeof(ITEM) < num, "Invalid Array[] index");	\
      return *(const ITEM *)((char *)((void *)data) + index*sizeof(ITEM));\
    }									\
    void append(ITEM const & item) { fxArray::append(&item); }		\
    void append(ARRAY const & a) { fxArray::append(a); }		\
    void remove(u_int start, u_int length=1)				\
	{ fxArray::remove(start,length); }				\
    ARRAY cut(u_int start, u_int len = 1);				\
    void insert(ARRAY const & a, u_int p)				\
	{ fxArray::insert(a,p); }					\
    void insert(ITEM const & item, u_int p)				\
	{ fxArray::insert(&item,p);}					\
    ARRAY extract(u_int start, u_int len);				\
    ARRAY head(u_int len = 1);						\
    ARRAY tail(u_int len = 1);						\
    int find(ITEM const& x, u_int start=0) const {			\
	return fxArray::find(&x,start);					\
    }									\
protected:								\
    ARRAY(u_int esize, u_int num, void *data);				\
public:									\
__enddef__

#define fxArrayVirtuals							\
protected:								\
    virtual void createElements(void *,u_int);				\
    virtual void destroyElements(void *,u_int);				\
    virtual void copyElements(void const*,void*,u_int) const;		\
    virtual int compareElements(void const *, void const *) const;  	\
__enddef__


//----------------------------------------------------------------------
// Declare an array containing items of type ITEM.

#define fxDECLARE_Array(ARRAY,ITEM)					\
class ARRAY : public fxArray {						\
public:									\
    fxArrayHeader(ARRAY,ITEM)						\
};									\
fxDECLARE_Ptr(ARRAY);							\
__enddef__

#define fxDECLARE_StructArray(ARRAY,ITEM) fxDECLARE_Array(ARRAY,ITEM)
#define fxDECLARE_PrimArray(ARRAY,ITEM)	fxDECLARE_Array(ARRAY,ITEM)

#define fxDECLARE_ObjArray(ARRAY,ITEM)					\
class ARRAY : public fxArray {						\
public:									\
    fxArrayHeader(ARRAY,ITEM)						\
    fxArrayVirtuals							\
};									\
fxDECLARE_Ptr(ARRAY);							\
__enddef__

#define fxDECLARE_PtrArray(ARRAY, POINTER)				\
class ARRAY : public fxArray {						\
public:									\
    fxArrayHeader(ARRAY,POINTER)					\
protected:								\
    virtual void createElements(void *, u_int);				\
};									\
fxDECLARE_Ptr(ARRAY);							\
__enddef__

//----------------------------------------------------------------------
// Various method implementations

#define fxIMPLEMENT_ArrayMethods(ARRAY,ITEM)				\
    ARRAY::ARRAY() : fxArray(sizeof(ITEM))				\
	{ if (data) createElements(data,num); }				\
    ARRAY::ARRAY(ARRAY const& a) : fxArray(a.elementsize) 		\
	{ maxi = a.maxi; num = a.num; data = a.raw_copy(); }		\
    ARRAY::ARRAY(u_int size) : fxArray(sizeof(ITEM),size)		\
	{ createElements(data,num); }					\
    ARRAY::~ARRAY() { destroy(); }					\
    const char* ARRAY::className() const { return fxQUOTE(ARRAY); }	\
    ARRAY ARRAY::cut(u_int start, u_int len)				\
	{return ARRAY(sizeof(ITEM), len*sizeof(ITEM),raw_cut(start,len));}\
    ARRAY ARRAY::extract(u_int start, u_int len)			\
	{return ARRAY(sizeof(ITEM), len*sizeof(ITEM),raw_extract(start,len));}\
    ARRAY ARRAY::head(u_int len)					\
	{return ARRAY(sizeof(ITEM), len*sizeof(ITEM),raw_head(len));}   \
    ARRAY ARRAY::tail(u_int len)					\
        {return ARRAY(sizeof(ITEM),len*sizeof(ITEM),raw_tail(len));}    \
    ARRAY::ARRAY(u_int esize, u_int num, void * data)		        \
	: fxArray(esize,num,data) {}					\
__enddef__

#define fxIMPLEMENT_ObjArrayMethods(ARRAY,ITEM)				\
    void ARRAY::createElements(void * start, u_int numbytes) {		\
	ITEM * ptr = (ITEM *)start;					\
	for (;;) {							\
	    if (numbytes == 0) break;					\
	    numbytes -= elementsize;					\
	    ITEM * obj = new(ptr) ITEM;					\
	    ptr++; 							\
	    (void) obj;							\
	}								\
    }									\
    void ARRAY::destroyElements(void * start, u_int numbytes) {		\
	ITEM * ptr = (ITEM *)start;					\
	while (numbytes) {						\
	    numbytes -= elementsize;					\
	    ptr->ITEM::~ITEM();						\
	    ptr++;							\
	}								\
    }									\
    void ARRAY::copyElements(void const * src, void * dst,		\
	    u_int numbytes) const {					\
	if (src<dst) {							\
	    src = (const char*)src + numbytes;				\
	    dst = (char*)dst + numbytes;				\
	    const ITEM * p = (const ITEM *)src - 1;			\
	    ITEM * q = (ITEM *)dst - 1;					\
	    while (numbytes > 0) {					\
		ITEM * obj = new(q) ITEM(*p);				\
		q--; p--;						\
		numbytes -= elementsize;				\
		(void) obj;						\
	    }								\
	} else {							\
	    const ITEM * p = (const ITEM *)src;				\
	    ITEM * q = (ITEM *)dst;					\
	    while (numbytes > 0) {					\
		ITEM * obj = new(q) ITEM(*p);				\
		q++; p++;						\
		numbytes -= elementsize;				\
		(void) obj;						\
	    }								\
	}								\
    }									\
    int ARRAY::compareElements(void const *o1, void const *o2) const	\
    {									\
	return ((const ITEM *)o1)->compare((const ITEM *)o2);		\
    }									\
__enddef__

#define fxIMPLEMENT_PtrArrayMethods(ARRAY,POINTER)			\
    void ARRAY::createElements(void * start, u_int numbytes) {		\
	memset(start,0,numbytes);					\
    }									\
__enddef__

//----------------------------------------------------------------------
// Implement various types of arrays

#define fxIMPLEMENT_Array(ARRAY,ITEM)					\
    fxIMPLEMENT_ArrayMethods(ARRAY,ITEM)				\
__enddef__

#define fxIMPLEMENT_PrimArray(ARRAY,ITEM)				\
    fxIMPLEMENT_ArrayMethods(ARRAY,ITEM)				\
__enddef__

#define fxIMPLEMENT_StructArray(ARRAY,ITEM) 				\
    fxIMPLEMENT_ArrayMethods(ARRAY,ITEM)				\
__enddef__

#define fxIMPLEMENT_ObjArray(ARRAY,ITEM)				\
    fxIMPLEMENT_ArrayMethods(ARRAY,ITEM)				\
    fxIMPLEMENT_ObjArrayMethods(ARRAY,ITEM)				\
__enddef__

#define fxIMPLEMENT_PtrArray(ARRAY,POINTER)				\
    fxIMPLEMENT_Array(ARRAY,POINTER)					\
    fxIMPLEMENT_PtrArrayMethods(ARRAY,POINTER)				\
__enddef__
#endif /* _ARRAY_ */
