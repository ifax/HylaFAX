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
#include "Array.h"
#include <stdlib.h>

#define that (*this)

fxArray::fxArray(u_short esize, u_int initlength)
{
    num = maxi = initlength * esize;
    elementsize = esize;
    if (maxi != 0)
	data = malloc((u_int) maxi);
    else
	data = 0;
    // Don't create the elements because the subclass will do that, because
    // we can't call a virtual from within this constructor.
}

fxArray::fxArray(const fxArray & other)
{
    num = other.num;
    maxi = other.num;
    elementsize = other.elementsize;
    data = 0;
    getmem();
    copyElements(other.data,data,num);
}

fxArray::fxArray(u_int esize, u_int n, void * d)
{
    elementsize=esize;
    num=maxi=n;
    data=d;
}

fxArray::~fxArray()
{
    if (data)
	free((void*) data);
}

void
fxArray::destroy()
{
    if (num != 0) destroyElements(data,num);
}

u_int fxArray::length() const { return num/elementsize; }

void
fxArray::append(void const * item) {
    assert(num<=maxi);
    if (num == maxi) expand();
    copyElements(item, data + num, elementsize);
    num += elementsize;
}

void
fxArray::append(const fxArray & a) {
    assert(elementsize == a.elementsize);
    u_int length = a.num;
    if (length > 0) {
	if (num + length > maxi) {
	    maxi = num + length;
	    getmem();
	}
	copyElements(a.data, data+num, length);
	num += length;
    }
}

void
fxArray::remove(u_int start, u_int length) {
  if (length>0) {
    start *= elementsize;
    length *= elementsize;
    assert(start+length <= num);
    destroyElements(data+start,length);
    if (start+length < num) {
	memmove((void*)(data + start),
	    (void*)(data + start+length), num - (start+length));
	// we don't use copyElements because they are just being moved.
    }
    num -= length;
    // we don't destroy the end elements because they still exist; they've
    // just been moved.
  }
}

void
fxArray::resize(u_int length) {
    length *= elementsize;
    maxi = length;
    if (length>num) {
	getmem();
	createElements(data + num, length - num);
    } else if (num>length) {
	destroyElements(data + length, num - length);
	getmem();
    }
    num = length;
}

void
fxArray::setMaxLength(u_int length)
{
    length *= elementsize;
    length = fxmax(length,num);
    if (maxi != length) {
	maxi = length;
	getmem();
    }
}

void
fxArray::createElements(void*, u_int)
{
}

void
fxArray::destroyElements(void*, u_int)
{
}

void
fxArray::copyElements(const void * source, void * dest, u_int length) const
{
    memmove(dest,source,length);
}

int
fxArray::compareElements(const void * e1, const void * e2) const
{
    return memcmp(e1,e2,elementsize);
}

void
fxArray::expand()
{ // by default, grab 4 more element spaces
    maxi += elementsize*4;
    getmem();
}

// this function keeps `data' up to date when maxi has just been changed
void
fxArray::getmem()
{
    if (maxi == 0) {
	if (data)
	    free((void*) data);
	data = 0;
    } else {
	if (data)
	    data = realloc(data,maxi);
	else
	    data = malloc(maxi);
    }
}

void
fxArray::insert(fxArray const & a, u_int posn)
{
    u_int length = a.num;
    if (a.length()>0) {
	assert(elementsize == a.elementsize);
	posn *= elementsize;
	assert(posn <= num);
	if (maxi < num + length) {
	    maxi = num + length;
	    getmem();
	}
	if (posn < num) {
	    memmove((void*)(data+posn+length), (void*)(data+posn), num-posn);
	    // we don't need to do a copyElements because we're not
	    // making new copies of objects, we're just moving
	    // existing ones.
	}
	copyElements(a.data, data+posn, length);
	num += length;
    }
}

void
fxArray::insert(void const * item, u_int posn)
{
    posn *= elementsize;
    assert(posn <= num);
    if (maxi <= num) {
	maxi = num+elementsize;
	getmem();
    }
    if (posn<num) {
	memmove((void*)(data+posn+(u_int)elementsize),
	    (void*)(data+posn), num-posn);
    }
    copyElements(item, data+posn, elementsize);
    num += elementsize;
}

#define TEMPSIZE 1024

void
fxArray::swap(u_int p1, u_int p2)
{
    char buffer[TEMPSIZE];
    void *tmp;
    p1 *= elementsize;
    p2 *= elementsize;
    if (elementsize>TEMPSIZE) tmp=malloc(elementsize);
    else tmp = buffer;
    memcpy(tmp,(void*)(data+p1),elementsize);
    memcpy((void*)(data+p1),(void*)(data+p2),elementsize);
    memcpy((void*)(data+p2),tmp,elementsize);
}

u_int
fxArray::find(void const * item, u_int start) const
{
    assert(start*elementsize <= num);
    fxAddress p = data + (u_int)(start*elementsize);
    while (p < data + num) {
	if (0 == compareElements(item,p)) return start;
	p = p+elementsize;
	start++;
    }
    return fx_invalidArrayIndex;
}

void
fxArray::qsortInternal(u_int l, u_int r, void * tmp)
{
    register u_int i=l;
    register u_int k=r+1;
    u_int e = elementsize;

    assert(k<=length());

    void * item = that[l];

    for (;;) {
	for (;;) {
            if(i>=r)break;
            ++i;
            if (compareElements(that[i],item) >= 0) break;
        }
        for (;;) {
            if (k<=l) break;
            --k;
            if (compareElements(that[k],item) <= 0) break;
        }
        if (i>=k) break;

	memcpy(tmp,that[i],e);
	memcpy(that[i],that[k],e);
	memcpy(that[k],tmp,e);
    }
    memcpy(tmp,that[l],e);
    memcpy(that[l],that[k],e);
    memcpy(that[k],tmp,e);
    if (k && l<k-1) qsortInternal(l,k-1,tmp);
    if (k+1 < r) qsortInternal(k+1,r,tmp);
}

#define SMALLBUFFERSIZE 32

void
fxArray::qsort(u_int posn, u_int len)
{
    if (len == 0) return;
    char smallbuffer[SMALLBUFFERSIZE];
    assert(posn+len <= num);
    void *tmp = (elementsize > SMALLBUFFERSIZE)
	? malloc(elementsize)
	: smallbuffer;
    qsortInternal(posn,posn+len-1,tmp);
    if (tmp != smallbuffer) free(tmp);
}

void
fxArray::qsort()
{
    qsort(0,length());
}

void *
fxArray::raw_extract(u_int start, u_int len) const
{
    if (len == 0) return 0;
    start *= elementsize;
    len *= elementsize;
    assert(start+len<=num);
    void * ret = malloc(len);
    copyElements(data+start, ret, len);
    return ret;
}

void *
fxArray::raw_cut(u_int start, u_int len)
{
    if (len == 0) return 0;
    start *= elementsize;
    len *= elementsize;
    assert(start+len <= num);
    void * ret = malloc(len);
    // we don't copy because we aren't making copies, we're just
    // moving existing elements from one array to another.
    memcpy(ret, (void*)(data+start), len);
    if (start+len < num) {
	// we don't use copyElements because they are just being moved.
	memmove((void*)(data + start),
	    (void*)(data + start+len), num - (start+len));
    }
    num -= len;
    return ret;
}

void *
fxArray::raw_copy() const
{
    if (num == 0) return 0;
    void * ret = malloc(num);
    copyElements(data,ret,num);
    return ret;
}

void *
fxArray::raw_head(u_int len) const
{
    if (len == 0) return 0;
    assert(len <= num);
    return raw_extract(0,len);
}

void *
fxArray::raw_tail(u_int len) const
{
    if (len == 0) return 0;
    len *= elementsize;
    assert(len <= num);
    void * ret = malloc(len);
    copyElements(data+(num-len), ret, len);
    return ret;
}
