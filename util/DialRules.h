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
#ifndef _DialStringRules_
#define	_DialStringRules_
/*
 * HylaFAX Dialing String Processing Rules.
 *
 * This file describes how to process user-specified dialing strings
 * to create two items:
 *
 * CanonicalNumber: a unique string that is derived from all dialing
 * strings to the same destination phone number.  This string is used
 * by the fax server for ``naming'' the destination. 
 *
 * DialString: the string passed to the modem for use in dialing the
 * telephone.
 */
#include "Str.h"

class RuleArray;
class RegExArray;
class VarDict;
class RulesDict;

class DialStringRules {
private:
    fxStr	filename;	// source of rules
    u_int	lineno;		// current line number during parsing
    FILE*	fp;		// open file during parsing
    VarDict*	vars;		// defined variables during parsing
    bool	verbose;	// trace parsing of rules file
    RegExArray*	regex;		// regular expressions
    RulesDict*	rules;		// rules defined in the file

    bool parseRules();
    bool parseRuleSet(RuleArray& rules);
    const char* parseToken(const char* cp, fxStr& v);
    char* nextLine(char* line, int lineSize);
    void subRHS(fxStr& v);
protected:
    virtual void parseError(const char* fmt ...);
    virtual void traceParse(const char* fmt ...);
    virtual void traceRules(const char* fmt ...);
public:
    DialStringRules(const char* filename);
    virtual ~DialStringRules();

    void setVerbose(bool b);
    u_int getLineno() const;
    const fxStr& getFilename() const;

    void def(const fxStr& var, const fxStr& value);
    void undef(const fxStr& var);

    bool parse(bool shouldExist = true);

    fxStr applyRules(const fxStr& name, const fxStr& s);
    fxStr canonicalNumber(const fxStr&);
    fxStr dialString(const fxStr&);
    fxStr displayNumber(const fxStr&);
};
inline u_int DialStringRules::getLineno() const		 { return lineno; }
inline const fxStr& DialStringRules::getFilename() const { return filename; }
#endif /* _DialStringRules_ */
