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
#ifndef _PAGESIZE_
#define	_PAGESIZE_
/*
 * Page size information.
 */
#if defined(c_plusplus) || defined(__cplusplus)
#include "Str.h"

class PageInfoArray;

typedef unsigned int BMU;	// ISO basic measurement unit
struct PageInfo {
    const char* name;	// page size name
    const char* abbr;	// abbreviated name
    BMU	w, h;		// nominal page width & height
    BMU	grw, grh;	// guaranteed reproducible width & height
    BMU	top;		// top margin for grh
    BMU	left;		// bottom margin for grw
};

class PageSizeInfo {
private:
    const PageInfo* info;

    friend class PageSizeInfoIter;

    PageSizeInfo(const PageInfo&);

    static PageInfoArray* pageInfo;

    static const PageInfo* getPageInfoByName(const char* name);
    static PageInfoArray* readPageInfoFile();
    static fxBool skipws(char*& cp,
		const char* file, const char* item, u_int lineno);

    static float toMM(BMU v)		{ return (v/1200.)*25.4; }
    static BMU fromMM(float v)		{ return BMU((v/25.4)*1200); }
public:
    PageSizeInfo();
    ~PageSizeInfo();

    static PageSizeInfo* getPageSizeByName(const char*);
    static PageSizeInfo* getPageSizeBySize(float w, float h);

    const char* name() const;		// fully qualified page size name
    const char* abbrev() const;		// abbreviated name
    float width() const;		// nominal page width (mm)
    float height() const;		// nominal page height (mm)
    float guarWidth() const;		// guaranteed reproducible width (mm)
    float guarHeight() const;		// guaranteed reproducible height (mm)
    float topMargin() const;		// top margin for GRA (mm)
    float leftMargin() const;		// left margin for GRA (mm)
};
inline const char* PageSizeInfo::name() const	{ return info->name; }
inline const char* PageSizeInfo::abbrev() const	{ return info->abbr; }
inline float PageSizeInfo::width() const	{ return toMM(info->w); }
inline float PageSizeInfo::height() const	{ return toMM(info->h); }
inline float PageSizeInfo::guarHeight() const	{ return toMM(info->grh); }
inline float PageSizeInfo::guarWidth() const	{ return toMM(info->grw); }
inline float PageSizeInfo::topMargin() const	{ return toMM(info->top); }
inline float PageSizeInfo::leftMargin() const	{ return toMM(info->left); }

/*
 * For iterating over the available page sizes.
 */
class PageSizeInfoIter {
private:
    PageSizeInfo pi;
    u_int	i;
public:
    PageSizeInfoIter();
    ~PageSizeInfoIter();
    void operator++();
    void operator++(int);
    operator const PageSizeInfo&();
    fxBool notDone();
};
#else
/*
 * C interface to get at information...
 */
struct pageSizeInfo;			/* opaque handle */
extern struct pageSizeInfo* getPageSize(const char* name);
extern struct pageSizeInfo* closestPageSize(float w, float h);
extern void delPageSize(struct pageSizeInfo*);
extern const char* getPageName(const struct pageSizeInfo*);
extern const char* getPageAbbrev(const struct pageSizeInfo*);
extern float getPageWidth(const struct pageSizeInfo*);
extern float getPageHeight(const struct pageSizeInfo*);
extern float getPageGuarHeight(const struct pageSizeInfo*);
extern float getPageGuarWidth(const struct pageSizeInfo*);
extern float getPageTopMargin(const struct pageSizeInfo*);
extern float getPageLeftMargin(const struct pageSizeInfo*);
#endif
#endif /* _PAGESIZE_ */
