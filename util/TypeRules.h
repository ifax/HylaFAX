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
#ifndef _TYPERULES_
#define	_TYPERULES_
/*
 * Fax Submission File Type and Conversion Rules.
 */
#include "Types.h"
#include "Str.h"
#include "Obj.h"

/*
 * A type rule database is an ASCII file that contains
 * type deduction and conversion rules.  The format is
 * as follows:
 *
 * HylaFAX file type rules.
 *
 * This file contains the file typing rules used by the sendfax
 * program to deduce how input files should be prepared for fax
 * transmission.  The format of this file is based on the System
 * V /etc/magic file used by the file(1) program.  The code that
 * reads this file was written entirely based on the comments that
 * exist at the top of the magic file and describe how it works.
 * The use of magic-style rules is intended to make it easier for
 * users to reuse rules already designed for use with file(1).
 *
 * The fields on each line are:
 *
 * offset: a byte offset in the file at which data should be extracted
 *	  and compared to a matching string or value.  If this value
 *	  begins with '>', then an additional rule is used and scanning
 *	  continues to the next type rule line that does not begin with
 *	  a '>'.
 * datatype: the type of data value to extract the specified offset in the
 *	  for comparison purposes.  This can be byte, short, long, or
 *	  string (a not necessarily null-terminated string of bytes).
 *	  A byte is 8 bits, short 16 bits, and long 32 bits.
 * match:  the value and operation to use in matching; the value used is
 *	  based on the datatype field.  This value may be "x" to mean
 *	  "match anything".  The operation is "=" if nothing is specified;
 *	  otherwise it can be one of ">", "<", "<=", ">=", "!=", "&"
 *	  (for checking if a set of bits is on), "^" (for xor-ing and
 *	  comparing to zero), and "!" (for checking if a set of bits is off).
 * result: one of "PS", "TIFF, or "error" (case insensitive).  The first
 *	  two results specifiy whether the rule generates a PostScript
 *	  file or a bilevel TIFF image.  The "error" result indicates a
 *	  file is unsuitable for transmission and if supplied as an
 *	  argument to sendfax, the command should be aborted.
 * rule:  a string passed to the shell to convert the input file
 *	  to the result format (suitable for sending as facsimile).
 *	  The rule string is a printf-like string that should use the
 *	  following "%" escapes:
 *		%i	input file name
 *		%o	output file name
 *		%r	output horizontal resolution in pixels/mm
 *		%R	output horizontal resolution in pixels/inch
 *		%v	output vertical resolution in lines/mm
 *		%V	output vertical resolution in lines/inch
 *		%f	data format, 1 for 1-d encoding or 2 for 2-d encoding
 *		%w	page width in mm
 *		%W	page width in pixels
 *		%l	page length in mm
 *		%L	page length in inches
 *		%s	page size by name
 *		%<x>	the <x> character (e.g. ``%%'' results in ``%''
 */
class TypeRule;
class TypeRuleArray;

class TypeRules {
public:
    TypeRules();
    ~TypeRules();

    static TypeRules* read(const fxStr& file);	// read rule database

    void setVerbose(bool);

    const TypeRule* match(const void* data, u_int size) const;
private:
    TypeRuleArray* rules;
    bool	verbose;			// while matching

    u_int match2(u_int base, const void* data, u_int size, bool verb) const;
};

typedef u_int TypeResult;		// conversion result

/*
 * Type rules specify how to convert a file that is
 * submitted for transmission into a format suitable
 * for the fax server.  File types are based on an
 * analysis of the file's ``magic number''.  Type
 * conversions are specified by a parameterized string
 * of commands to pass to a shell.
 */
class TypeRule : public fxObj {
public:
    enum {
	TIFF,		// bilevel Group 3-encoded TIFF
	POSTSCRIPT,	// PostScript
	ERROR		// recognized erronious format
    };
private:
    off_t	off;	// byte offset in file
    bool	cont;	// continuation
    enum {
	ASCII,		// ascii-only string
	ASCIIESC,	// ascii-only string + escape char (iso-2022 variants)
	STRING,		// byte string
	ADDR,		// address of match
	BYTE,		// 8 bits
	SHORT,		// 16 bits
	LONG		// 32 bits
    } type;		// data value type
    enum {
	ANY,		// match anything
	EQ,		// == value
	NE,		// != value
	LT,		// < value
	LE,		// <= value
	GT,		// > value
	GE,		// >= value
	AND,		// (&value) != 0
	XOR,		// (^value) != 0
	NOT		// (!value) != 0
    } op;		// match operation
    union {
	long  v;
	char* s;
    } value;		// matching value
    TypeResult	result;	// result of applying rule
    fxStr	cmd;	// shell command/error message

    friend TypeRules* TypeRules::read(const fxStr& file);
public:
    TypeRule();
    TypeRule(const TypeRule& other);
    virtual ~TypeRule();

    bool	match(const void*, u_int size, bool verbose = false) const;
    bool	isContinuation() const;

    TypeResult	getResult() const;
    const fxStr& getCmd() const;
    fxStr getErrMsg() const;
    fxStr getFmtdCmd(const fxStr& input, const fxStr& output,
		    float hr, float vr,
		    const fxStr& df,
		    const fxStr& pname) const;
};
inline bool TypeRule::isContinuation() const	{ return cont; }
inline TypeResult TypeRule::getResult() const	{ return result; }
inline const fxStr& TypeRule::getCmd() const	{ return cmd; }
inline fxStr TypeRule::getErrMsg() const	{ return cmd; }
#endif /* _TYPERULES_ */
