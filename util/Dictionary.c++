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
#include "Dictionary.h"

#define DEFAULTSIZE 31

fxIMPLEMENT_PtrArray(fxDictBuckets,fxDictBucket *)
fxIMPLEMENT_PtrArray(fxDictIters, fxDictIter *)

#define KEYAT(base) (base)
#define	VALUEAT(base) (void*)(((char*)base) + keysize)

#define KEY(b) KEYAT((b)->kvmem)
#define VALUE(b) VALUEAT((b)->kvmem)

fxDictionary::fxDictionary(u_int ksize, u_int vsize,u_int initsize)
{
    if (initsize == 0) initsize = DEFAULTSIZE;
    buckets.resize(initsize);
    keysize = ksize;
    valuesize = vsize;
    numItems = 0;
}

fxDictionary::fxDictionary(const fxDictionary& a)
{
    for (int i = 0; i < a.buckets.length(); i++) {
	const fxDictBucket* sb = a.buckets[i];
	while (sb) {
	    addInternal(KEY(sb),VALUE(sb));
	    sb = sb->next;
	}
    }
}

fxDictionary::~fxDictionary()
{
}

void fxDictionary::cleanup()
{
    u_int i;
    u_int l = buckets.length();
    for (i=0; i < l; i++) {
	fxDictBucket* bucket = buckets[i];
	while (bucket) {
	    fxDictBucket* bucket2 = bucket->next;
	    destroyKey(KEY(bucket));
	    destroyValue(VALUE(bucket));
	    delete bucket;
	    bucket = bucket2;
	}
	buckets[i] = 0;
    }
    l = iters.length();
    for (i=0; i < l; i++) {
	iters[i]->dict = 0;
	iters[i]->node = 0;
	iters[i]->invalid = true;
    }
}

fxDictBucket::~fxDictBucket()
{
    if (kvmem) free(kvmem);
}

void
fxDictionary::operator=(const fxDictionary &a)
{
    assert(keysize == a.getKeySize());
    assert(valuesize == a.getValueSize());
    if (this == &a) return;
    this->fxDictionary::~fxDictionary();	// NB: need this for HP C++ 3.4
    for (int i = 0; i < a.buckets.length(); i++) {
	const fxDictBucket* db = a.buckets[i];
	while (db) {
	    addInternal(KEY(db), VALUE(db));
	    db = db->next;
	}
    }
}

void
fxDictionary::addInternal(const void* key, const void* value)
{
    u_int index = (u_int)(hashKey(key) % buckets.length());
    fxDictBucket* bucket = buckets[index];
    while (bucket && compareKeys(key,KEY(bucket))) {
	bucket = bucket->next;
    }
    if (bucket) {	 // just change the value
	destroyValue(VALUE(bucket));
	copyValue(value,VALUE(bucket));
	return;
    } else {
	void * kvmem = malloc(keysize + valuesize);
	copyKey(key,KEYAT(kvmem));
	copyValue(value,VALUEAT(kvmem));
	fxDictBucket* newbucket = new fxDictBucket(kvmem,buckets[index]);
	buckets[index] = newbucket;
	numItems++;
    }
}

const void*
fxDictionary::find(const void* key, const void **keyp) const
{
    u_int index = (u_int)(hashKey(key) % buckets.length());
    const fxDictBucket* bucket = buckets[index];
    while (bucket && compareKeys(key,KEY(bucket))) bucket = bucket->next;
    if (bucket) {
	if (keyp != 0) *keyp = &KEY(bucket);
	return VALUE(bucket);
    } else {
	if (keyp != 0) *keyp = 0;
	return 0;
    }
}

void*
fxDictionary::findCreate(const void* key)
{
    u_int index = (u_int)(hashKey(key) % buckets.length());
    fxDictBucket* bucket = buckets[index];
    while (bucket && compareKeys(key,KEY(bucket))) bucket = bucket->next;
    if (bucket)
	return VALUE(bucket);
    else {
	char* kvmem = (char*)malloc(keysize + valuesize);
	copyKey(key,KEYAT(kvmem));
	createValue(VALUEAT(kvmem));
	fxDictBucket* bucket = new fxDictBucket(kvmem,buckets[index]);
	buckets[index] = bucket;
	numItems++;
	return VALUEAT(kvmem);
    }
}

void
fxDictionary::remove(const void* key)
{
    u_int index = (u_int)(hashKey(key) % buckets.length());
    fxDictBucket* bucket = buckets[index];
    fxDictBucket** prev = &buckets[index];
    while (bucket && compareKeys(key,KEY(bucket))) {
	prev = &bucket->next;
	bucket = bucket->next;
    }
    if (bucket) {
	*prev = bucket->next;
	destroyKey(KEY(bucket));
	destroyValue(VALUE(bucket));
	invalidateIters(bucket);
	delete bucket;
	numItems--;
    }
}

void *
fxDictionary::cut(const void* key)
{
    u_int index = (u_int)(hashKey(key) % buckets.length());
    fxDictBucket* bucket = buckets[index];
    fxDictBucket** prev = &buckets[index];
    while (bucket && compareKeys(key,KEY(bucket))) {
	prev = &bucket->next;
	bucket = bucket->next;
    }
    if (bucket) {
	*prev = bucket->next;
	void * v = malloc(valuesize);
	memcpy(v,VALUE(bucket),valuesize);
	destroyKey(KEY(bucket));
	// no need to destroy value because it has moved to *v
	invalidateIters(bucket);
	delete bucket;
	numItems--;
	return v;
    } else {
	return 0;
    }
}

u_long
fxDictionary::hashKey(const void* key) const
{
    u_long u = 0;
    const u_long* p = (const u_long*)key;
    int l = (int)keysize;
    while (l>=sizeof (u_long)) {
	u ^= *p++;
	l -= sizeof (u_long);
    }
    return u;
}

void fxDictionary::destroyKey(void *) const { }
void fxDictionary::destroyValue(void *) const { }

void
fxDictionary::addIter(fxDictIter* i)
{
    iters.append(i);
}

void
fxDictionary::removeIter(fxDictIter* i)
{
    iters.remove(iters.find(i));
}

void
fxDictionary::invalidateIters(const fxDictBucket *db)
{
    u_int l = iters.length();
    for (u_int i = 0; i<l; i++) {
	fxDictIter* di = iters[i];
	if (di->node == db) {
	    (*di)++;
	    if (di->dict) di->invalid = true;
	}
    }
}

//----------------------------------------------------------------------

fxDictIter::fxDictIter()
{
    invalid = false;
    dict = 0;
    bucket = 0;
    node = 0;
}

fxDictIter::fxDictIter(fxDictionary& d)
{
    invalid = false;
    dict = &d;
    bucket = 0;
    node = d.buckets[bucket];
    d.addIter(this);
    if (!node) advanceToValid();
}

fxDictIter::~fxDictIter()
{
    if (dict) dict->removeIter(this);
}

void
fxDictIter::operator=(fxDictionary& d)
{
    if (dict) dict->removeIter(this);
    dict = &d;
    bucket = 0;
    node = d.buckets[bucket];
    invalid = false;
    d.addIter(this);
    if (!node) advanceToValid();
}

void *
fxDictIter::getKey() const
{
    if (invalid) return 0;
    else return KEY(node);
}

void *
fxDictIter::getValue() const
{
    if (invalid) return 0;
    else return (char *)node->kvmem + dict->keysize;
}

void
fxDictIter::increment()
{
    if (!dict) return;
    if (invalid) {
	invalid = false;
	return;
    }
    node = node->next;
    if (node) return;

    // otherwise, find another node!
    advanceToValid();
}

void
fxDictIter::advanceToValid()
{
    u_int len = dict->buckets.length();
    fxDictBucket * n;
    for(;;) {
	bucket++;
	assert(bucket<=len);
	if (bucket == len) {
	    dict->removeIter(this);		// it's done with us
	    dict = 0;
	    invalid = true;
	    break;
	}
	if (n = dict->buckets[bucket]) {	// NB: intentional =
	    node = n;
	    invalid = false;
	    break;
	}
    }
}
