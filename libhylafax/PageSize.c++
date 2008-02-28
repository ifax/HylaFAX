/*	$Id$ */
/*
 * Copyright (c) 1993-1996 Sam Leffler
 * Copyright (c) 1993-1996 Silicon Graphics, Inc.
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
 * Page Size Support.
 */
#include "Types.h"
#include "PageSize.h"
#include "Array.h"
#include "config.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "NLS.h"

fxDECLARE_StructArray(PageInfoArray, PageInfo)
fxIMPLEMENT_StructArray(PageInfoArray, PageInfo)

PageInfoArray* PageSizeInfo::pageInfo = NULL;

#include <stdarg.h>

static void
parseError(const char* file, u_int lineno, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, NLS::TEXT("%s: line %u: "), file, lineno);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

bool
PageSizeInfo::skipws(char*& cp,
    const char* file, const char* item, u_int lineno)
{
    if (isspace(*cp))
	*cp++ = '\0';
    while (isspace(*cp))
	cp++;
    if (*cp == '\0') {
	parseError(file, lineno,
	    NLS::TEXT("Unexpected end of line after \"%s\".\n"), item);
	return (false);
    } else
	return (true);
}

PageInfoArray*
PageSizeInfo::readPageInfoFile()
{
    fxStr file = FAX_LIBDATA;
    file.append("/");
    file.append(FAX_PAGESIZES);
    PageInfoArray* info = new PageInfoArray;
    FILE* fp = fopen(file, "r");
    u_int lineno = 0;
    if (fp) {
	char line[1024];
	while (fgets(line, sizeof (line), fp)) {
	    lineno++;
	    char* cp = strchr(line, '#');
	    if (cp)
		*cp = '\0';
	    else if ((cp = strchr(line, '\n')))
		*cp = '\0';
	    for (cp = line; isspace(*cp); cp++)
		;
	    if (*cp == '\0')
		continue;
	    // name<tab>width<ws>height<ws>gwidth<ws>gheight<ws>top<ws>left
	    PageInfo pi;
	    pi.name = cp;
	    while (*cp != '\t')
		cp++;
	    if (!skipws(cp, file, NLS::TEXT("page size name"), lineno))
		continue;
	    pi.abbr = cp;
	    while (*cp != '\t')
		cp++;
	    if (!skipws(cp, file, NLS::TEXT("page size abbreviation"), lineno))
		continue;
	    pi.w = (BMU) strtoul(cp, &cp, 10);
	    if (!skipws(cp, file, NLS::TEXT("page width"), lineno))
		continue;
	    pi.h = (BMU) strtoul(cp, &cp, 10);
	    if (!skipws(cp, file, NLS::TEXT("page height"), lineno))
		continue;
	    pi.grw = (BMU) strtoul(cp, &cp, 10);
	    if (!skipws(cp, file, NLS::TEXT("guaranteed page width"), lineno))
		continue;
	    pi.grh = (BMU) strtoul(cp, &cp, 10);
	    if (!skipws(cp, file, NLS::TEXT("guaranteed page height"), lineno))
		continue;
	    pi.top = (BMU) strtoul(cp, &cp, 10);
	    if (!skipws(cp, file, NLS::TEXT("top margin"), lineno))
		continue;
	    pi.left = (BMU) strtoul(cp, &cp, 10);
	    pi.name = strdup(pi.name);
	    pi.abbr = strdup(pi.abbr);
	    info->append(pi);
	}
	fclose(fp);
    } else {
	fprintf(stderr,
    NLS::TEXT("Warning, no page size database file \"%s\", using builtin default.\n"),
	    (const char*)file);
	PageInfo pi;
	pi.name = strdup("default");
	pi.abbr = strdup("NA-LET");
	// North American Letter
	pi.w = 10200;
	pi.h = 13200;
	pi.grw = 9240;
	pi.grh = 12400;
	pi.top = 472;
	pi.left = 345;
	info->append(pi);
    }
    return info;
}

const PageInfo*
PageSizeInfo::getPageInfoByName(const char* name)
{
    int c = tolower(name[0]);
    size_t len = strlen(name);
    for (int i = 0, n = pageInfo->length(); i < n; i++) {
	const PageInfo& pi = (*pageInfo)[i];
	if (strncasecmp(pi.abbr, name, len) == 0)
	    return &pi;
	for (const char* cp = pi.name; *cp != '\0'; cp++)
	    if (tolower(*cp) == c && strncasecmp(cp, name, len) == 0)
		return &pi;
    }
    return (NULL);
}

PageSizeInfo*
PageSizeInfo::getPageSizeByName(const char* name)
{
    if (pageInfo == NULL)
	pageInfo = readPageInfoFile();
    const PageInfo* info = getPageInfoByName(name);
    return info ? new PageSizeInfo(*info) : (PageSizeInfo*) NULL;
}

PageSizeInfo*
PageSizeInfo::getPageSizeBySize(float wmm, float hmm)
{
    BMU w = fromMM(wmm);
    BMU h = fromMM(hmm);

    if (pageInfo == NULL)
	pageInfo = readPageInfoFile();
    int best = 0;
    u_long bestMeasure = (u_long)-1;
    for (int i = 0, n = pageInfo->length(); i < n; i++) {
	int dw = (int)((*pageInfo)[i].w - w);
	int dh = (int)((*pageInfo)[i].h - h);
	u_long measure = dw*dw + dh*dh;
	if (measure < bestMeasure) {
	    best = i;
	    bestMeasure = measure;
	}
    }
#define THRESHOLD 720000		// .5" in each dimension
    return bestMeasure < THRESHOLD ?
	new PageSizeInfo((*pageInfo)[best]) : (PageSizeInfo*) NULL;
}

PageSizeInfo::PageSizeInfo()
{
    if (pageInfo == NULL)
	pageInfo = readPageInfoFile();
    info = getPageInfoByName("default");
}
PageSizeInfo::PageSizeInfo(const PageInfo& i) : info(&i) {}
PageSizeInfo::~PageSizeInfo() {}

/*
 * C stub interfaces...
 */
extern "C" struct pageSizeInfo*
getPageSize(const char* name)
{
    return (struct pageSizeInfo*) PageSizeInfo::getPageSizeByName(name);
}

extern "C" struct pageSizeInfo*
closestPageSize(float w, float h)
{
    return (struct pageSizeInfo*) PageSizeInfo::getPageSizeBySize(w,h);
}

extern "C" void
delPageSize(struct pageSizeInfo* info)
{
    delete (PageSizeInfo*) info;
}

extern "C" const char*
getPageName(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->name();
}

extern "C" const char*
getPageAbbrev(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->abbrev();
}

extern "C" float
getPageWidth(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->width();
}

extern "C" float
getPageHeight(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->height();
}

extern "C" float
getPageGuarHeight(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->guarHeight();
}

extern "C" float
getPageGuarWidth(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->guarWidth();
}

extern "C" float
getPageTopMargin(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->topMargin();
}

extern "C" float
getPageLeftMargin(const struct pageSizeInfo* info)
{
     return ((const PageSizeInfo*) info)->leftMargin();
}

/*
 * Page size iterator support.
 */

PageSizeInfoIter::PageSizeInfoIter()
{
    i = 0;
}
PageSizeInfoIter::~PageSizeInfoIter()		{}
void PageSizeInfoIter::operator++()		{ i++; }
void PageSizeInfoIter::operator++(int)		{ i++; }
PageSizeInfoIter::operator const PageSizeInfo&()
{
    if (i < PageSizeInfo::pageInfo->length())
	pi.info = &(*PageSizeInfo::pageInfo)[i];
    return (pi);
}
bool PageSizeInfoIter::notDone()
    { return i < PageSizeInfo::pageInfo->length(); }
