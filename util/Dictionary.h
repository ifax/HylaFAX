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
#ifndef _Dictionary_
#define	_Dictionary_

#include "Types.h"
#include "Array.h"
#include "Str.h"
#include "DSmacros.h"
#include "stdlib.h"

class fxDictIter;
class fxDictionary;

/*******************************************
//  fxDictionary interface
class DICT(KEY,VALUE) : public fxDictionary {
public:
    DICT();
    DICT(const DICT &);
    virtual ~DICT();
    u_int size() const;
    u_int getKeySize() const;
    u_int getValueSize() const;
    void operator=(const DICT &);
    void add(const KEY &,const VALUE &);
    void remove(const KEY);
    VALUE *cut(const KEY);			// does a find & remove
    const VALUE* find(const KEY&, const KEY** k = 0) const;
	// returns null if not found, optionally returns pointer to key
    VALUE& operator[](const KEY &);		// creates the key/value if not
    const VALUE& operator[](const KEY &) const;	// creates the key/value if not
    fxObj* copy() const;
protected:
    virtual u_long hashKey(const void *) const;
    virtual int compareKeys(const void *, void const *) const; // 0 => equal
    virtual void copyKey(const void *, void *) const;
    virtual void copyValue(const void *, void *) const;
    virtual void destroyKey(void *) const;
    virtual void destroyValue(void *) const;
    virtual void createValue(void *) const;
}

class DICTIter(DICT,KEY,VALUE) : public fxDictIter {
public:
    DICTIter();
    DICTIter(DICT&);
    operator=(DICT&);
    operator++();
    operator++(int);
    operator KEY const &() const
    KEY const& key() const;
    VALUE& value() const;
    fxBool removed();
    fxBool notDone();
}
*******************************************/

//----------------------------------------------------------------------

class fxDictBucket {
public:
    fxDictBucket(void* kv,fxDictBucket* n) { kvmem = kv; next = n; }
    ~fxDictBucket();

    void* kvmem;
    fxDictBucket* next;
};

fxDECLARE_PtrArray(fxDictBuckets, fxDictBucket*)
fxDECLARE_PtrArray(fxDictIters, fxDictIter*)

//----------------------------------------------------------------------

#define fxDictVirtuals(HOW)						\
    virtual u_long hashKey(const void*) const;				\
    virtual int compareKeys(const void*, const void*) const HOW;	\
    virtual void copyKey(const void*, void*) const HOW;			\
    virtual void destroyKey(void*) const;				\
    virtual void copyValue(const void*, void*) const HOW;		\
    virtual void destroyValue(void*) const;				\
    virtual void createValue(void*) const HOW;				\
__enddef__

//----------------------------------------------------------------------

class fxDictionary : public fxObj {
    friend class fxDictIter;
public:
    u_int size() const { return numItems; }
    u_int getKeySize() const { return keysize; }
    u_int getValueSize() const { return valuesize; }
    virtual char const *className() const = 0;

protected:
    fxDictionary(u_int ksize, u_int vsize, u_int initsize = 0);
    fxDictionary(const fxDictionary&);
    virtual ~fxDictionary();

    void cleanup();

    void operator=(const fxDictionary&);

    const void *find(const void*, const void** k = 0) const;
    void *findCreate(const void*);
    void remove(const void*);
    void *cut(const void*);

    virtual void addInternal(const void*, const void*);

    fxDictVirtuals(= 0)

    void addIter(fxDictIter*);
    void removeIter(fxDictIter*);
    void invalidateIters(const fxDictBucket*);

    u_int numItems;
    u_int keysize;
    u_int valuesize;
    fxDictBuckets buckets;
    fxDictIters iters;
};

//----------------------------------------------------------------------

class fxDictIter {
    friend class fxDictionary;
public:
    fxDictIter();
    ~fxDictIter();
    fxDictIter(fxDictionary&);
    void operator=(fxDictionary&);	// not a const argument!
    void operator++()	   { increment(); }
    void operator++(int)   { increment(); }
    fxBool removed() const { return invalid; }
    fxBool notDone() const { return node != 0; }
protected:
    void* getKey() const;
    void* getValue() const;

    void increment();
    void advanceToValid();

    fxDictionary* dict;			// 0 if iterator isn't valid yet.
    u_int bucket;        
    u_int invalid:1;			// this is 1 if the element was deleted.
    fxDictBucket* node; 		// if "invalid", points to the next node
};

//----------------------------------------------------------------------
// Iterator declaration & implementation macros

#define fxDECLARE_DictIter(DICT,KEY,VALUE)				\
class fxCAT(DICT,Iter) : public fxDictIter {/*XXX*/			\
public:									\
    fxCAT(DICT,Iter)();							\
    fxCAT(DICT,Iter)(DICT &);						\
    fxCAT(DICT,Iter)(DICT *);						\
    void operator=(DICT& d)	/* not a const argument! */		\
	{ fxDictIter::operator=(d); }					\
    void operator=(DICT* d)	/* not a const argument! */		\
	{ fxDictIter::operator=(*d); }					\
    operator KEY const &() const					\
	{ return *(KEY *)fxDictIter::getKey(); }			\
    KEY const &key() const						\
	{ return *(KEY *)fxDictIter::getKey(); }			\
    VALUE &value() const						\
	{ return *(VALUE *)fxDictIter::getValue(); }			\
};									\
__enddef__

#define fxIMPLEMENT_DictIter(DICT,KEY,VALUE)				\
    fxCAT(DICT,Iter)::fxCAT(DICT,Iter)() : fxDictIter()	{}		\
    fxCAT(DICT,Iter)::fxCAT(DICT,Iter)(DICT &d)				\
	: fxDictIter((fxDictionary &)d) {}				\
    fxCAT(DICT,Iter)::fxCAT(DICT,Iter)(DICT *d)				\
	: fxDictIter(*(fxDictionary *)d) {}				\
__enddef__

//----------------------------------------------------------------------
// Dictionary declaration macros

#define	fxNOTHING
#define fxDECLARE_Dictionary(DICT,KEY,VALUE)				\
class DICT : public fxDictionary {/*XXX*/				\
public:									\
    DICT(u_int initsize=0);						\
    ~DICT();								\
    virtual char const *className() const;				\
    void operator=(const DICT &d)					\
	{fxDictionary::operator=((const fxDictionary &)d);}		\
    void add(const KEY &k, const VALUE &v) { addInternal(&k,&v); }	\
    VALUE* cut(const KEY k)						\
	{ return (VALUE*)fxDictionary::cut(&k); }			\
    void remove(const KEY k)						\
	{ fxDictionary::remove(&k); }					\
    const VALUE* find(const KEY& k, const KEY** kp = 0) const		\
	{ return (const VALUE*)fxDictionary::find(&k, (const void**)&kp); }\
    VALUE& operator[](const KEY& k)					\
	{ return *(VALUE*)fxDictionary::findCreate(&k); }		\
protected:								\
    fxDictVirtuals(fxNOTHING)						\
};									\
fxDECLARE_Ptr(DICT);							\
fxDECLARE_DictIter(DICT,KEY,VALUE)					\
__enddef__

// StrDictionary: the key is a fxStr object. The
// user only has to define copyValue and destroyValue.

#define fxDECLARE_StrKeyDictionary(DICT,VALUE) \
    fxDECLARE_Dictionary(DICT,fxStr,VALUE)
#define fxDECLARE_PtrKeyDictionary(DICT,KEY,VALUE) \
    fxDECLARE_Dictionary(DICT,KEY,VALUE)
#define fxDECLARE_ObjKeyDictionary(DICT,KEY,VALUE) \
    fxDECLARE_Dictionary(DICT,KEY,VALUE)

//----------------------------------------------------------------------
// Dictionary method macros.  Used by the implement macros.

#define fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    DICT::DICT(u_int initsize) :					\
        fxDictionary(sizeof(KEY),sizeof(VALUE),initsize) {} 		\
    DICT::~DICT() { cleanup(); }					\
    char const *DICT::className() const { return fxQUOTE(DICT); }	\
    fxIMPLEMENT_DictIter(DICT,KEY,VALUE)				\
__enddef__

#define fxIMPLEMENT_StrKeyDictionaryMethods(DICT,VALUE)			\
    fxIMPLEMENT_copyObj(DICT,Key,fxStr)					\
    fxIMPLEMENT_destroyObj(DICT,Key,fxStr)				\
    u_long DICT::hashKey(const void* key) const				\
	{ return ((const fxStr*)key)->hash(); }				\
    int DICT::compareKeys(const void* key1, const void* key2) const	\
	{ return ::compare(*(const fxStr*)key1,*(const fxStr*)key2); }	\
__enddef__

#define fxIMPLEMENT_ObjKeyDictionaryMethods(DICT,KEY,VALUE)		\
    fxIMPLEMENT_copyObj(DICT,Key,KEY)					\
    fxIMPLEMENT_destroyObj(DICT,Key,KEY)				\
    int DICT::compareKeys(const void* key1, const void* key2) const	\
	{ return ((const KEY*)key1)->compare((const KEY*)key2); }	\
__enddef__

#define fxIMPLEMENT_PtrKeyDictionaryMethods(DICT,KEY,VALUE)		\
    fxIMPLEMENT_copyPtr(DICT,Key,KEY)					\
    fxIMPLEMENT_destroyPtr(DICT,Key,KEY)				\
    u_long DICT::hashKey(const void* key) const				\
	{ return u_long(((*(const u_int*)(key)) >> 2)); }		\
    int DICT::compareKeys(const void* key1, const void* key2) const	\
	{ return (*(const char**)key1 - *(const char**)key2); }		\
__enddef__

#define fxIMPLEMENT_ObjValueDictionaryMethods(DICT,VALUE)		\
    fxIMPLEMENT_copyObj(DICT,Value,VALUE)				\
    fxIMPLEMENT_destroyObj(DICT,Value,VALUE)				\
    fxIMPLEMENT_createObj(DICT,Value,VALUE)				\
__enddef__

#define fxIMPLEMENT_PtrValueDictionaryMethods(DICT,VALUE)		\
    fxIMPLEMENT_copyPtr(DICT,Value,VALUE)				\
    fxIMPLEMENT_destroyPtr(DICT,Value,VALUE)				\
    fxIMPLEMENT_createPtr(DICT,Value,VALUE)				\
__enddef__

//----------------------------------------------------------------------
// Dictionary implementation macros.  These are used by the programmer
// to implement new dictionary types.

#define fxIMPLEMENT_Dictionary(DICT,KEY,VALUE)				\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
__enddef__

#define fxIMPLEMENT_StrKeyDictionary(DICT,VALUE)			\
    fxIMPLEMENT_DictionaryMethods(DICT,fxStr,VALUE)			\
    fxIMPLEMENT_StrKeyDictionaryMethods(DICT,VALUE)			\
__enddef__

#define fxIMPLEMENT_ObjKeyDictionary(DICT,KEY,VALUE)			\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_ObjKeyDictionaryMethods(DICT,KEY,VALUE)			\
__enddef__

#define fxIMPLEMENT_PtrKeyDictionary(DICT,KEY,VALUE)			\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_PtrKeyDictionaryMethods(DICT,KEY,VALUE)			\
__enddef__

#define fxIMPLEMENT_ObjValueDictionary(DICT,VALUE)			\
    fxIMPLEMENT_ObjValueDictionaryMethods(DICT,VALUE)			\
__enddef__

#define fxIMPLEMENT_PtrValueDictionary(DICT,VALUE)			\
    fxIMPLEMENT_PtrValueDictionaryMethods(DICT,VALUE)			\
__enddef__

#define fxIMPLEMENT_ObjKeyObjValueDictionary(DICT,VALUE)		\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_ObjKeyDictionaryMethods(DICT, VALUE)			\
    fxIMPLEMENT_ObjValueDictionaryMethods(DICT, VALUE)			\
__enddef__

#define fxIMPLEMENT_StrKeyObjValueDictionary(DICT,VALUE)		\
    fxIMPLEMENT_DictionaryMethods(DICT,fxStr,VALUE)			\
    fxIMPLEMENT_StrKeyDictionaryMethods(DICT, VALUE)			\
    fxIMPLEMENT_ObjValueDictionaryMethods(DICT, VALUE)			\
__enddef__

#define fxIMPLEMENT_StrKeyPtrValueDictionary(DICT,VALUE)		\
    fxIMPLEMENT_DictionaryMethods(DICT,fxStr,VALUE)			\
    fxIMPLEMENT_StrKeyDictionaryMethods(DICT,VALUE)			\
    fxIMPLEMENT_PtrValueDictionaryMethods(DICT,VALUE)			\
__enddef__

#define fxIMPLEMENT_PtrKeyPtrValueDictionary(DICT,KEY,VALUE)		\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_PtrKeyDictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_PtrValueDictionaryMethods(DICT,VALUE)			\
__enddef__

#define fxIMPLEMENT_PtrKeyObjValueDictionary(DICT,KEY,VALUE)		\
    fxIMPLEMENT_DictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_PtrKeyDictionaryMethods(DICT,KEY,VALUE)			\
    fxIMPLEMENT_ObjValueDictionaryMethods(DICT,VALUE)			\
__enddef__
#endif /* _Dictionary_ */
