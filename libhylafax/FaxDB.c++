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
#include "FaxDB.h"
#include "RE.h"

FaxDBRecord::FaxDBRecord()
{
    parent = 0;
}

FaxDBRecord::FaxDBRecord(FaxDBRecord* other)
{
    if ((parent = other))
	parent->inc();
}

FaxDBRecord::~FaxDBRecord()
{
    if (parent)
	parent->dec();
}

const char* FaxDBRecord::className() const { return "FaxDBRecord"; }

const fxStr FaxDBRecord::nullStr("");

const fxStr&
FaxDBRecord::find(const fxStr& key)
{
    const fxStr* s = 0;
    FaxDBRecord* rec = this;
    for (; rec && !(s = rec->dict.find(key)); rec = rec->parent)
	;
    return (s ? *s : nullStr);
}

void FaxDBRecord::set(const fxStr& key, const fxStr& value)
    { dict[key] = value; }

fxIMPLEMENT_StrKeyObjValueDictionary(FaxValueDict, fxStr)

FaxDB::FaxDB(const fxStr& file) : filename(file)
{
    FILE* fd = fopen(file, "r");
    if (fd) {
	lineno = 0;
	parseDatabase(fd, 0);
	fclose(fd);
    }
}

FaxDB::~FaxDB()
{
}

const char* FaxDB::className() const { return "FaxDB"; }

fxStr FaxDB::nameKey("Name");
fxStr FaxDB::numberKey("FAX-Number");
fxStr FaxDB::companyKey("Company");
fxStr FaxDB::locationKey("Location");
fxStr FaxDB::phoneKey("Voice-Number");

FaxDBRecord*
FaxDB::find(const fxStr& s, fxStr* name)
{
    fxStr canon(s);
    canon.lowercase();
    for (u_int l = 0; l < canon.length(); l = canon.next(l, "+?*[].\\")) {
	canon.insert('\\', l);
	l += 2;
    }
    RE pat(canon);
    for (FaxInfoDictIter iter(dict); iter.notDone(); iter++) {
	fxStr t(iter.key());
	t.lowercase();
	if (pat.Find(t)) {
	    if (name)
		*name = iter.key();
	    return (iter.value());
	}
    }
    return (0);
}

FaxDBRecord* FaxDB::operator[](const fxStr& name)	{ return dict[name]; }
const fxStr& FaxDB::getFilename()			{ return filename; }
FaxInfoDict& FaxDB::getDict()				{ return dict; }
void FaxDB::add(const fxStr& key, FaxDBRecord* r)	{ dict[key] = r; }

void
FaxDB::parseDatabase(FILE* fd, FaxDBRecord* parent)
{
    FaxDBRecordPtr rec(new FaxDBRecord(parent));
    fxStr key;
    while (getToken(fd, key)) {
	if (key == "]") {
	    if (parent == 0)
		fprintf(stderr, _("%s: line %d: Unmatched \"]\".\n"),
		    (const char*) filename, lineno);
	    break;
	}
	if (key == "[") {
	    parseDatabase(fd, rec);    		// recurse to form hierarchy
	    continue;
	}
	fxStr value;
	if (!getToken(fd, value))
	    break;
	if (value != ":") {
	    fprintf(stderr, _("%s: line %d: Missing \":\" separator.\n"),
		(const char*) filename, lineno);
	    continue;
	}
	if (!getToken(fd, value))
	    break;
	rec->set(key, value);
	if (key == nameKey)			// XXX what about duplicates?
	    add(value, rec);
    }
}

#include "StackBuffer.h"
#include <ctype.h>

bool
FaxDB::getToken(FILE* fd, fxStr& token)
{
    int c;
top:
    if ((c = getc(fd)) == EOF)
	return (false);
    while (isspace(c)) {
	if (c == '\n')
	    lineno++;
	c = getc(fd);
    }
    if (c == '#') {
	while ((c = getc(fd)) != EOF && c != '\n')
	    ;
	if (c == EOF)
	    return (false);
	lineno++;
	goto top;
    }
    if (c == '[' || c == ']' || c == ':') {
	char buf[2];
	buf[0] = c;
	buf[1] = '\0';
	token = buf;
	return (true);
    }
    fxStackBuffer buf;
    if (c == '"') {
	while ((c = getc(fd)) != EOF) {
	    if (c == '\\') {
		c = getc(fd);
		if (c == EOF) {
		    fprintf(stderr, _("%s: Premature EOF.\n"), (const char*) filename);
		    return (false);
		}
		// XXX handle standard escapes
		if (c == '\n')
		    lineno++;
	    } else {
		if (c == '"')
		    break;
		if (c == '\n')
		    lineno++;
	    }
	    buf.put(c);
	}
    } else {
	do
	    buf.put(c);
	while ((c = getc(fd)) != EOF && !isspace(c) &&
	  c != ':' && c != ']' && c != '[' && c != '#');
	if (c != EOF)
	    ungetc(c, fd);
    }
    buf.set('\0');
    token = (const char*) buf;
    return (true);
}

#ifdef notdef
void
FaxDB::write()
{
    fxStr temp(filename | "#");
    FILE* fp = fopen(temp, "w");
    if (fp) {
	write(fp);
	fclose(fp);
	::rename(temp, filename);
    }
}

void
FaxDB::write(FILE* fp)
{
    for (FaxInfoDictIter iter(dict); iter.notDone(); iter++) {
	fprintf(fp, "[ Name: \"%s\"\n", (char*) iter.key());
	FaxDBRecord* r = iter.value();
	const fxStr&
	fprintf(fp, "]\n");
    }
}
#endif

fxIMPLEMENT_StrKeyPtrValueDictionary(FaxInfoDict, FaxDBRecord*)
