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
#ifndef _FaxDB_
#define	_FaxDB_

#include "Dictionary.h"
#include "Ptr.h"

fxDECLARE_StrKeyDictionary(FaxValueDict, fxStr)

class FaxDBRecord : public fxObj {
protected:
    FaxDBRecord*	parent;		// parent in hierarchy
    FaxValueDict	dict;		// key-value map

    static const fxStr nullStr;

    friend class FaxDB;
public:
    FaxDBRecord();
    FaxDBRecord(FaxDBRecord* other);
    ~FaxDBRecord();

    const char* className() const;

    const fxStr& find(const fxStr& key);
    void set(const fxStr& key, const fxStr& value);
};

fxDECLARE_Ptr(FaxDBRecord);
fxDECLARE_StrKeyDictionary(FaxInfoDict, FaxDBRecordPtr)

class FaxDB : public fxObj {
protected:
    fxStr	filename;
    int		lineno;			// for parsing
    FaxInfoDict dict;			// name->record map

    void parseDatabase(FILE*, FaxDBRecord* parent);
    fxBool getToken(FILE*, fxStr& token);
public:
    FaxDB(const fxStr& filename);
    ~FaxDB();

    FaxDB* dup() { referenceCount++; return this; }
    const char* className() const;

    static fxStr nameKey;
    static fxStr numberKey;
    static fxStr locationKey;
    static fxStr phoneKey;
    static fxStr companyKey;

    FaxDBRecord* find(const fxStr& pat, fxStr* name = 0);
    FaxDBRecord* operator[](const fxStr& name);
    const fxStr& getFilename();
    FaxInfoDict& getDict();
    void add(const fxStr& key, FaxDBRecord*);
};
#endif /* _FaxDB_ */
