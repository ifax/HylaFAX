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
#include <ctype.h>

#include "Array.h"
#include "TypeRules.h"
#include "PageSize.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
extern "C" {
#include <netinet/in.h>
}

TypeRule::TypeRule() {}
TypeRule::~TypeRule() {}
TypeRule::TypeRule(const TypeRule& other)
    : fxObj(other)
    , cmd(other.cmd)
{
    off = other.off;
    cont = other.cont;
    type = other.type;
    op = other.op;
    value.v = other.value.v;
    result = other.result;
}

static const char* typeNames[] =
    { "ascii", "string", "address", "byte", "short", "long" };
static const char* opNames[] =
    { "<any>", "=", "!=", "<", "<=", ">", ">=", "&", "^", "!" };
static const char* resultNames[] = { "tiff", "postscript", "error" };

fxBool
TypeRule::match(const void* data, u_int size, fxBool verbose) const
{
    if (verbose) {
	printf("rule: %soffset %#lx %s %s",
	    cont ? ">" : "",
	    (u_long) off,
	    typeNames[type],
	    opNames[op]
	);
	if (type == STRING)
	    printf(" \"%s\"", value.s);
	else if (type != ASCII) {
	    if (op == ANY)
		printf(" <any value>");
	    else
		printf(" %#x", value.v);
	}
	printf(" -- ");
    }
    if (off > size) {
	if (verbose)
	    printf("failed (offset past data)\n");
	return (FALSE);
    }
    fxBool ok = FALSE;
    u_long v = 0;
    const u_char* cp = (const u_char*) data;
    switch (type) {
    case ASCII:
	u_int i;
	for (i = 0; i < size; i++)
	    if (!isprint(cp[i]) && !isspace(cp[i])) {
		if (verbose)
		    printf("failed (unprintable char %#x)\n", cp[i]);
		return (FALSE);
	    }
	ok = TRUE;
	goto done;
    case STRING:
	ok = (strncmp((const char*)(cp+off), value.s,
	    fxmin((u_int) strlen(value.s), (u_int)(size-off))) == 0);
	goto done;
    case ADDR:
	v = (u_long) off;
	break;
    case BYTE:
	v = *cp;
	break;
    case SHORT:
	if (off + 2 < size) {
	    u_short w;
	    memcpy(&w, cp+off, 2);
	    v = ntohs(w);
	    break;
	}
	if (verbose)
	    printf("failed (insufficient data)\n");
	return (FALSE);
    case LONG:
	if (off + 4 < size) {
	    memcpy(&v, cp+off, 4);
	    v = ntohl(v);
	    break;
	}
	if (verbose)
	    printf("failed (insufficient data)\n");
	return (FALSE);
    }
    /*
     * Numeric value, use operation.
     */
    switch (op) {
    case ANY:	ok = TRUE; break;
    case EQ:	ok = (v == value.v); break;
    case NE:	ok = (v != value.v); break;
    case LT:	ok = (v  < value.v); break;
    case LE:	ok = (v <= value.v); break;
    case GT:	ok = (v  > value.v); break;
    case GE:	ok = (v >= value.v); break;
    case AND:	ok = ((v&value.v) == value.v); break;
    case NOT:	ok = ((v&value.v) != value.v); break;
    case XOR:	ok = ((v^value.v) != 0); break;
    }
done:
    if (verbose) {
	if (ok)
	    printf("success (result %s, rule \"%s\")\n",
		resultNames[result], (const char*) cmd);
	else
	    printf("failed (comparison)\n");
    }
    return (ok);
}

/*
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
 *		%F	the pathname of the fax library (e.g./usr/local/lib/fax)
 *		%<x>	the <x> character (e.g. ``%%'' results in ``%''
 */
fxStr
TypeRule::getFmtdCmd(
    const fxStr& input, const fxStr& output,
    float hr, float vr, const fxStr& df, const fxStr& pname) const
{
    fxStr fmtd;
    const PageSizeInfo* info = PageSizeInfo::getPageSizeByName(pname);
    float pw = info->width();
    float pl = info->height();

    for (u_int i = 0, n = cmd.length(); i < n; i++) {
	char c = cmd[i];
	if (c == '%' && i+1 < n) {
	    i++;
	    switch (c = cmd[i]) {
	    case 'i':	fmtd.append(input);			  continue;
	    case 'o':	fmtd.append(output);			  continue;
	    case 'R':	fmtd.append(fxStr(hr, "%.2f"));		  continue;
	    case 'r':	fmtd.append(fxStr(hr/25.4, "%.2g"));	  continue;
	    case 'V':	fmtd.append(fxStr(vr, "%.2f"));		  continue;
	    case 'v':	fmtd.append(fxStr(vr/25.4, "%.2g"));	  continue;
	    case 'f':	fmtd.append(df);			  continue;
	    case 'W':	fmtd.append(fxStr(pw, "%.2g"));		  continue;
	    case 'w':	fmtd.append(fxStr((pw*hr)/25.4, "%.2g")); continue;
	    case 'L':	fmtd.append(fxStr(pl, "%.2g"));		  continue;
	    case 'l':	fmtd.append(fxStr((pl*vr)/25.4, "%.2g")); continue;
	    case 'F':	fmtd.append(fxStr(FAX_LIBEXEC));	  continue;
	    case 's':	fmtd.append(pname);			  continue;
	    }
	}
	fmtd.append(c);
    }
    return fmtd;
}

fxDECLARE_ObjArray(TypeRuleArray, TypeRule)
fxIMPLEMENT_ObjArray(TypeRuleArray, TypeRule)

TypeRules::TypeRules()
{
    verbose = FALSE;
    rules = new TypeRuleArray;
}

TypeRules::~TypeRules()
{
    delete rules;
}

void
TypeRules::setVerbose(fxBool b)
{
    verbose = b;
}

#include <ctype.h>

static fxBool
appendLine(fxStr& s, const char* line)
{
    const char* cp = line;
    for (; isspace(*cp); cp++)
	;
    if (cp > line)		// put back one bit of white space
	cp--;
    const char* cmd = cp;
    if (*cp != '\0' && *cp != '\n' && *cp != '#') {
	do {
	    cp++;
	    if (cp[-1] == '\\') {
		if (*cp == '\n') {	// continue cmd string on next line
		    s.append(cmd, cp-1 - cmd);
		    return (TRUE);
		} else if (*cp)		// accept anything but \\0
		    cp++;
	    }
	} while (*cp != '\0' && *cp != '\n' && *cp != '#');
	s.append(fxStr(cmd, cp-cmd));
    }
    return (FALSE);
}

#include <stdarg.h>

static void
parseError(const char* file, u_int lineno, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: line %u: ", file, lineno); 
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

TypeRules*
TypeRules::read(const fxStr& file)
{
    FILE* fp;

    fp = fopen(file, "r");
    if (fp == NULL) {
	fprintf(stderr, "%s: Can not open type rules file.\n",
	    (const char*) file);
	return NULL;
    }
    TypeRules* tr = new TypeRules;
    char line[256];
    u_int lineno = 0;
    while (fgets(line, sizeof (line), fp) != NULL) {
	lineno++;
	char* cp = line;
	if (*cp == '\n' || *cp == '#')
	    continue;
	TypeRule rule;
	if (*cp == '>') {		// continuation
	    rule.cont = TRUE;
	    cp++;
	} else
	    rule.cont = FALSE;
	const char *op = cp;
	rule.off = strtoul(op, &cp, 0);	// file offset
	if (cp == op) {
	    parseError(file, lineno, "Missing file offset");
	    continue;
	}
	while (isspace(*cp))
	    cp++;
	const char* tp = cp;
	while (*cp && !isspace(*cp))	// data type
	    cp++;
	if (strncasecmp(tp, "byte", cp-tp) == 0)
	    rule.type = TypeRule::BYTE;
	else if (strncasecmp(tp, "short", cp-tp) == 0)
	    rule.type = TypeRule::SHORT;
	else if (strncasecmp(tp, "long", cp-tp) == 0)
	    rule.type = TypeRule::LONG;
	else if (strncasecmp(tp, "string", cp-tp) == 0)
	    rule.type = TypeRule::STRING;
	else if (strncasecmp(tp, "ascii", cp-tp) == 0)
	    rule.type = TypeRule::ASCII;
	else if (strncasecmp(tp, "addr", cp-tp) == 0)
	    rule.type = TypeRule::ADDR;
	else {
	    parseError(file, lineno, "Unknown datatype \"%.*s\"", cp-tp, tp);
	    continue;			// bad type
	}
	while (isspace(*cp))
	    cp++;
	rule.op = TypeRule::EQ;		// default is '='
	const char* vp = cp;
	if (rule.type != TypeRule::STRING && rule.type != TypeRule::ASCII) {
	    // numeric value
	    switch (*vp) {
	    case '=':	rule.op = TypeRule::EQ;	cp++; break;
	    case '^':	rule.op = TypeRule::XOR; cp++; break;
	    case '&':	rule.op = TypeRule::AND; cp++; break;
	    case 'x':	rule.op = TypeRule::ANY; cp++; break;
	    case '>':
		if (cp[1] == '=') {
		    rule.op = TypeRule::GE; cp++;
		} else
		    rule.op = TypeRule::GT;
		cp++;
		break;
	    case '<':
		if (cp[1] == '=') {
		    rule.op = TypeRule::LE; cp++;
		} else
		    rule.op = TypeRule::LT;
		cp++;
		break;
	    case '!':
		if (cp[1] == '=') {
		    rule.op = TypeRule::NE; cp++;
		} else
		    rule.op = TypeRule::NOT;
		cp++;
		break;
	    }
	    if (rule.op != TypeRule::ANY) {
		const char* vp = cp;
		rule.value.v = strtol(vp, &cp, 0);
		if (vp == cp) {
		    parseError(file, lineno, "Missing match value");
		    continue;
		}
	    }
	} else {			// string value
	    while (*cp != '\0' && *cp != '\t')	// NB: accept blanks
		cp++;
	    if (*cp != '\t')
		continue;
	    u_int len = cp-vp;
	    rule.value.s = (char*) malloc(len+1);	// +1 for \0
	    memcpy(rule.value.s, vp, len);
	    rule.value.s[len] = '\0';
	}
	while (isspace(*cp))
	    cp++;
	const char* rp = cp;
	while (isalpha(*cp))
	    cp++;
	if (strncasecmp(rp, "tiff", cp-rp) == 0)
	    rule.result = TypeRule::TIFF;
	else if (strncasecmp(rp, "ps", cp-rp) == 0)
	    rule.result = TypeRule::POSTSCRIPT;
	else if (strncasecmp(rp, "error", cp-rp) == 0)
	    rule.result = TypeRule::ERROR;
	else {
	    parseError(file, lineno, "Unknown result \"%.*s\"", cp-rp, rp);
	    continue;
	}
	while (isspace(*cp))
	    cp++;
	const char* cmd = cp;		// collect cmd string
	if (*cp != '\0' && *cp != '\n' && *cp != '#') {
	    fxBool done = FALSE;	// XXX workaround compiler limitation
	    do {
		cp++;
		if (cp[-1] == '\\') {
		    if (*cp == '\n') {	// continue cmd string on next line
			rule.cmd = fxStr(cmd, cp-1 - cmd);
			while (fgets(line, sizeof (line), fp) != NULL &&
			  appendLine(rule.cmd, line))
			    ;
			done = TRUE;
		    } else if (*cp)	// accept anything but \\0
			cp++;
		}
	    } while (!done && *cp != '\0' && *cp != '\n' && *cp != '#');
	    if (!done)
		rule.cmd = fxStr(cmd, cp-cmd);
	}
	tr->rules->append(rule);
    }
    (void) fclose(fp);
    return (tr);
}

/*
 * Check secondary matching rules after a primary match.
 */
u_int
TypeRules::match2(u_int base, const void* data, u_int size, fxBool verb) const
{
    for (u_int i = 1, n = (*rules).length() - base; i < n; i++) {
	TypeRule& rule = (*rules)[base+i];
	if (!rule.isContinuation())
	    break;
	if (rule.match(data, size, verb))
	    return (i);
    }
    return 0;
}

const TypeRule*
TypeRules::match(const void* data, u_int size) const
{
    if (verbose)
	printf("match against (..., %u)\n", size);
    for (u_int i = 0, n = (*rules).length(); i < n; i++) {
	TypeRule& rule = (*rules)[i];
	if (!rule.isContinuation() && rule.match(data, size, verbose))
	    return (&(*rules)[i + match2(i, data, size, verbose)]);
    }
    if (verbose)
	printf("no match\n");
    return (NULL);
}
