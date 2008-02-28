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

/*
 * HylaFAX Server Configuration Base Class.
 */
#include "FaxConfig.h"
#include "Str.h"

#include <ctype.h>
#include <pwd.h>

#include "Sys.h"
#include "NLS.h"


FaxConfig::FaxConfig()
{
    lineno = 0;
    lastModTime = 0;
}
FaxConfig::FaxConfig(const FaxConfig& other)
{
    lineno = other.lineno;
    lastModTime = other.lastModTime;
}
FaxConfig::~FaxConfig() {}

void
FaxConfig::readConfig(const fxStr& filename)
{
    FILE* fd = Sys::fopen(tildeExpand(filename), "r");
    if (fd) {
	configTrace(NLS::TEXT("Read config file %s"), (const char*) filename);
	char line[1024];
	while (fgets(line, sizeof (line)-1, fd)){
	    line[strlen(line)-1]='\0';		// Nuke \r at end of line
	    (void) readConfigItem(line);
	}
	fclose(fd);
    }
}
void FaxConfig::resetConfig() { lineno = 0; }

bool
FaxConfig::updateConfig(const fxStr& filename)
{
    struct stat sb;
    fxStr path(tildeExpand(filename));
    if (Sys::stat(path, sb) == 0 && sb.st_mtime > lastModTime) {
	resetConfig();
	readConfig(path);
	lastModTime = sb.st_mtime;
	return (true);
    } else
	return (false);
}

/*
 * Expand a filename that might have `~' in it.
 */
fxStr
FaxConfig::tildeExpand(const fxStr& filename)
{
    fxStr path(filename);
    if (filename.length() > 1 && filename[0] == '~') {
	path.remove(0);
	const char* cp = getenv("HOME");
	if (!cp || *cp == '\0') {
	    struct passwd* pwd = getpwuid(getuid());
	    if (!pwd) {
		configError(NLS::TEXT("No passwd file entry for uid %u,"
		    " cannot expand ~ in \"%s\""),
		    getuid(), (const char*) filename);
		cp = "";		// NB: XXX maybe this should be fatal?
	    } else
		cp = pwd->pw_dir;
	}
	path.insert(cp);
    }
    return (path);
}

bool
FaxConfig::findTag(const char* tag, const void* names0, u_int n, u_int& ix)
{
    const tags* names = (const tags*) names0;

    for (int i = n-1; i >= 0; i--) {
	const char* cp = names[i].name;
	if (cp[0] == tag[0] && streq(cp, tag)) {
	    ix = i;
	    return (true);
	}
    }
    return (false);
}

bool
FaxConfig::findValue(const char* value, const char* values[], u_int n, u_int& ix)
{
    for (u_int i = 0; i < n; i++) {
	const char* cp = values[i];
	if (cp[0] == value[0] && streq(cp, value)) {
	    ix = i;
	    return (true);
	}
    }
    return (false);
}

int
FaxConfig::getNumber(const char* s)
{
    return ((int) strtol(s, NULL, 0));
}

#define	valeq(a,b)	(strcasecmp(a,b)==0)

bool
FaxConfig::getBoolean(const char* cp)
{
    return (valeq(cp, "on") || valeq(cp, "yes") || valeq(cp, "true"));
}

bool
FaxConfig::readConfigItem(const char* b)
{
    char buf[2048];
    char* cp;

    lineno++;
    strncpy(buf, b, sizeof (buf));
    for (cp = buf; isspace(*cp); cp++)		// skip leading white space
	;
    if (*cp == '#' || *cp == '\0')
	return (true);
    const char* tag = cp;			// start of tag
    while (*cp && *cp != ':') {			// skip to demarcating ':'
	if (isupper(*cp))
	    *cp = tolower(*cp);
	cp++;
    }
    if (*cp != ':') {
	configError(NLS::TEXT("Syntax error at line %u, missing ':' in \"%s\""),
	    lineno, b);
	return (false);
    }
    for (*cp++ = '\0'; isspace(*cp); cp++)	// skip white space again
	;
    const char* value;
    if (*cp == '"') {				// "..." value
	int c;
	/*
	 * Parse quoted string and deal with \ escapes.
	 */
	char* dp = ++cp;
	for (value = dp; (c = *cp) != '"'; cp++) {
	    if (c == '\0') {			// unmatched quote mark
		configError(NLS::TEXT("Syntax error at line %u, missing quote mark in \"%s\""),
		    lineno, b);
		return (false);
	    }
	    if (c == '\\') {
		c = *++cp;
		if (isdigit(c)) {		// \nnn octal escape
		    int v = c - '0';
		    if (isdigit(c = cp[1])) {
			cp++, v = (v << 3) + (c - '0');
			if (isdigit(c = cp[1]))
			    cp++, v = (v << 3) + (c - '0');
		    }
		    c = v;
		} else {			// \<char> escapes
		    for (const char* tp = "n\nt\tr\rb\bf\fv\013"; *tp; tp += 2)
			if (c == tp[0]) {
			    c = tp[1];
			    break;
			}
		}
	    }
	    *dp++ = c;
	}
	*dp = '\0';
    } else {					// value up to 1st non-ws
	for (value = cp; *cp && !isspace(*cp); cp++)
	    ;
	*cp = '\0';
    }

    if (streq(tag, "include") ) {
        u_int old_lineno = lineno;
	configTrace(NLS::TEXT("%s = %s (line %u)"), tag, value, lineno);
	lineno = 0;
	readConfig(value);
	lineno = old_lineno;
	return (true);
    }
    if (!setConfigItem(tag, value)) {
	configTrace(NLS::TEXT("Unknown configuration parameter \"%s\" ignored at line %u"),
	     tag, lineno);
	return (false);
    } else {
	configTrace(NLS::TEXT("%s = %s (line %u)"), tag, value, lineno);
	return (true);
    }
}
