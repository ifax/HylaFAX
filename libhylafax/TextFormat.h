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
#ifndef _TextFormat_
#define	_TextFormat_
/*
 * Simple Text To PostScript Conversion Support.
 */
#include "Str.h"
#include "FaxConfig.h"

typedef long TextCoord;		// local coordinates

class TextFont {
private:
    enum { maxaliases = 10 };		// max no of aliases to find a font
    fxStr	family;			// font family name
    fxStr	setproc;		// PostScript setfont procedure
    fxStr	showproc;		// PostScript show procedure
    TextCoord	widths[256];		// width table

    static fxStr fontMap;		// location of Fontmap file
    static fxStr fontPath;		// path for afm files
    static u_int fontID;		// font identifier number

    friend class TextFormat;

    void loadFixedMetrics(TextCoord w);
    FILE* openAFMFile(fxStr& pathname);
    bool getAFMLine(FILE* fp, char* buf, int bsize);
    static bool decodeFontName(const char*, fxStr&, fxStr&);
public:
    TextFont(const char*);
    ~TextFont();

    static bool findFont(const char* name);

    void defFont(FILE*, TextCoord pointSize, bool useISO8859) const;
    void setfont(FILE*) const;
    TextCoord show(FILE*, const char*, int len) const;
    TextCoord show(FILE*, const fxStr&) const;
    TextCoord strwidth(const char*) const;
    TextCoord charwidth(const char c) const;

    const char* getFamily(void) const;

    bool readMetrics(TextCoord pointsize, bool useISO8859, fxStr& emsg);
};
/*
 * we have to use const u_char
 */
inline TextCoord TextFont::charwidth(const char c) const { return widths[(u_char) c]; }
inline const char* TextFont::getFamily(void) const	 { return family; }

class FontDict;
class OfftArray;

class TextFormat : public FaxConfig {
public:
    enum {			// page orientation
	LANDSCAPE,
	PORTRAIT
    };
    enum {			// page collation
	FORWARD,
	REVERSE
    };
private:
    bool	gaudy;		// emit gaudy headers
    bool	landscape;	// horizontal landscape mode output
    bool	useISO8859;	// use the ISO 8859-1 character encoding
    bool	reverse;	// page reversal flag
    bool	wrapLines;	// wrap/truncate lines
    bool	headers;	// emit page headers
    bool	workStarted;	// formatting work begun
    FILE*	output;		// output file stream
    FILE*	tf;		// temporary output file
    OfftArray*	pageOff;	// page offset table
    int		firstPageNum;	// starting page number
    fxStr	curFile;	// current input filename (for header)
    fxStr	modDate;	// last modification date for input file
    fxStr	modTime;	// last modification time for input file
    fxStr	title;		// document title information
    FontDict*	fonts;		// font dictionary
    TextFont*	curFont;	// current font for imaging text

    float	physPageHeight;	// physical page height (inches)
    float	physPageWidth;	// physical page width (inches)
    TextCoord	pointSize;	// font point size in big points
    TextCoord	lm, rm;		// left, right margins in local coordinates
    TextCoord	tm, bm;		// top, bottom margin in local coordinates
    TextCoord	lineHeight;	// inter-line spacing
    bool	boc;		// at beginning of a column
    bool	bop;		// at beginning of a page
    bool	bol;		// at beginning of a line
    bool	bot;		// at beginning of a text string
    int		numcol;		// number of text columns
    int		column;		// current text column # (1..numcol)
    TextCoord	col_margin;	// inter-column margin
    TextCoord	col_width;	// column width in local coordinates
    int		level;		// PS string parenthesis level
    TextCoord	outline;	// page and column outline linewidth
    TextCoord	pageHeight;	// page height in local coordinates
    int		pageNum;	// current page number
    TextCoord	pageWidth;	// page width in local coordinates
    TextCoord	right_x;	// column width (right hand side x)
    int		tabStop;	// n-column tab stop
    TextCoord	tabWidth;	// tab stop width in local units
    TextCoord	x, y;		// current coordinate
    TextCoord	xoff;		// current x offset on line

    void putISOPrologue(void);
    void emitPrologue(void);
    void emitTrailer(void);

    void Copy_Block(off_t ,off_t);
protected:
    virtual void emitClientComments(FILE*);
    virtual void emitClientPrologue(FILE*);

    virtual void warning(const char* fmt ...) const;
    virtual void error(const char* fmt ...) const;
    virtual void fatal(const char* fmt ...) const;

    virtual void setupConfig(void);
    virtual bool setConfigItem(const char* tag, const char* value);
    virtual void configError(const char* fmt ...);
    virtual void configTrace(const char* fmt ...);
public:
    TextFormat();
    virtual ~TextFormat();

    virtual void resetConfig(void);

    static TextCoord inch(const char*);

    void setNumberOfColumns(u_int n);
    void setPageHeaders(bool);
    bool getPageHeaders(void) const;
    void setLineWrapping(bool);
    bool getLineWrapping(void) const;
    void setISO8859(bool);
    bool getISO8859(void) const;
    bool setTextFont(const char* fontName);
    void setGaudyHeaders(bool);
    bool setPageMargins(const char*);
    void setPageMargins(TextCoord l, TextCoord r, TextCoord b, TextCoord t);
    void setOutlineMargin(TextCoord);
    void setTextPointSize(TextCoord);
    TextCoord getTextPointSize(void) const;
    void setPageOrientation(u_int);
    bool setPageSize(const char*);
    void setPageWidth(float);
    void setPageHeight(float);
    void setPageCollation(u_int);
    void setTextLineHeight(TextCoord);
    TextCoord getTextLineHeight(void) const;
    void setTitle(const char*);

    void setFilename(const char*);
    void setModTimeAndDate(time_t);
    void setModTime(const char*);
    void setModDate(const char*);

    void setOutputFile(FILE*);
    FILE* getOutputFile(void);
    void flush(void);

    void beginFormatting(FILE* output);
    void endFormatting(void);
    void beginFile(void);
    void formatFile(const char*);
    void formatFile(FILE*);
    void endFile(void);
    void format(FILE*);
    void format(const char*, u_int cc);

    void newPage(void);
    void newCol(void);
    void beginCol(void);
    void endCol(void);
    void beginLine();
    void endLine(void);
    void beginText(void);
    void endTextLine(void);
    void endTextCol(void);
    void closeStrings(const char* cmd);

    TextFont* addFont(const char* name, const char* family);
    const TextFont* getFont(void) const;
    const TextFont* getFont(const char* name) const;
    void setFont(TextFont*);
    void setFont(const char*);
    void setFontPath(const char*);

    void hrMove(TextCoord);
    TextCoord getXOff(void) const;
    TextCoord getRHS(void) const;
    void reserveVSpace(TextCoord);
};

inline bool TextFormat::getLineWrapping(void) const	{ return wrapLines; }
inline bool TextFormat::getPageHeaders(void) const	{ return headers; }
inline TextCoord TextFormat::getTextPointSize(void) const	{ return pointSize; }
inline TextCoord TextFormat::getXOff(void) const		{ return xoff; }
inline TextCoord TextFormat::getRHS(void) const		{ return right_x; }
inline TextCoord TextFormat::getTextLineHeight(void) const	{ return lineHeight; }
inline bool TextFormat::getISO8859(void) const		{ return useISO8859; }
inline const TextFont* TextFormat::getFont(void) const	{ return curFont; }
inline FILE* TextFormat::getOutputFile(void)		{ return tf; }
#endif /* _TextFormat_ */
