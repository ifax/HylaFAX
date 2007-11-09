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
 * Text to PostScript conversion and formatting support.
 *
 * This class takes ASCII text and produces PostScript
 * doing formatting using the font metric information
 * for a single font.  Multi-column output, landscape and
 * portrait page orientation, and various other controls
 * are supported.  Page headers a la the old enscript
 * program from Adobe can also be added.  This code is
 * distantly related to the lptops program by Nelson Beebe.
 */
#include "config.h"
#include "Array.h"
#include "Dictionary.h"
#include "PageSize.h"
#include "TextFormat.h"
#include "Sys.h"

#include <ctype.h>
#include <errno.h>
#if HAS_MMAP
#include <sys/mman.h>
#endif

#define LUNIT 	(72*20)		// local coord system is .05 scale
#define	ICVT(x) ((TextCoord)((x)*LUNIT))	// scale inches to local coordinates
#define	CVTI(x)	(float(x)/LUNIT)	// convert coords to inches

inline TextCoord fxmax(TextCoord a, TextCoord b)
    { return (a > b) ? a : b; }

#define COLFRAC		35	// 1/fraction of col width for margin

fxDECLARE_PrimArray(OfftArray, off_t)
fxIMPLEMENT_PrimArray(OfftArray, off_t)
fxDECLARE_StrKeyDictionary(FontDict, TextFont*)
fxIMPLEMENT_StrKeyPtrValueDictionary(FontDict, TextFont*)

TextFormat::TextFormat()
{
    output = NULL;
    tf = NULL;
    pageOff = new OfftArray;

    firstPageNum = 1;		// starting page number
    column = 1;			// current text column # (1..numcol)
    pageNum = 1;		// current page number
    workStarted = false;

    fonts = new FontDict;
    curFont = addFont("Roman", "Courier");

    TextFormat::setupConfig();	// NB: virtual
}

TextFormat::~TextFormat()
{
    for (FontDictIter iter(*fonts); iter.notDone(); iter++)
	delete iter.value();
    delete fonts;
    if (tf != NULL)
	fclose(tf);
    tf = NULL;
}

void
TextFormat::warning(const char* fmt ...) const
{
    fputs(_("Warning, "), stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".\n", stderr);
}

void
TextFormat::error(const char* fmt ...) const
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".\n", stderr);
}

void
TextFormat::fatal(const char* fmt ...) const
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".\n", stderr);
    exit(1);
}

TextFont*
TextFormat::addFont(const char* name, const char* family)
{
    TextFont* f = new TextFont(family);
    (*fonts)[name] = f;
    if (workStarted) {
	fxStr emsg;
	if (!f->readMetrics(pointSize, useISO8859, emsg))
	    error(_("Font %s: %s"), f->getFamily(), (const char*) emsg);
    }
    return (f);
}

const TextFont*
TextFormat::getFont(const char* name) const
{
    return (*fonts)[name];
}

void TextFormat::setFont(TextFont* f)		{ curFont = f; }
void TextFormat::setFont(const char* name)		{ curFont = (*fonts)[name]; }	
void TextFormat::setFontPath(const char* path)	{ TextFont::fontPath = path; }	

void TextFormat::setOutputFile(FILE* f)		{ output = f; }
void TextFormat::setNumberOfColumns(u_int n)	{ numcol = n; }
void TextFormat::setPageHeaders(bool b)		{ headers = b; }
void TextFormat::setISO8859(bool b)		{ useISO8859 = b; }
void TextFormat::setLineWrapping(bool b)		{ wrapLines = b; }
void TextFormat::setOutlineMargin(TextCoord o)	{ outline = o; }
void TextFormat::setTextPointSize(TextCoord p)	{ pointSize = p; }
void TextFormat::setPageOrientation(u_int o)	{ landscape = (o == LANDSCAPE); }
void TextFormat::setPageCollation(u_int c)		{ reverse = (c == REVERSE); }
void TextFormat::setTextLineHeight(TextCoord h)	{ lineHeight = h; }
void TextFormat::setTitle(const char* cp)		{ title = cp; }
void TextFormat::setFilename(const char* cp)	{ curFile = cp; }

void
TextFormat::setGaudyHeaders(bool b)	
{
    if (gaudy = b)
	headers = true;
}

bool
TextFormat::setTextFont(const char* name)
{
    if (TextFont::findFont(name)) {
	(*fonts)["Roman"]->family = name;
	return (true);
    } else
	return (false);
}


/*
 * Parse margin syntax: l=#,r=#,t=#,b=#
 */
bool
TextFormat::setPageMargins(const char* s)
{
    for (const char* cp = s; cp && cp[0];) {
	if (cp[1] != '=')
	    return (false);
	TextCoord v = inch(&cp[2]);
	switch (tolower(cp[0])) {
	case 'b': bm = v; break;
	case 'l': lm = v; break;
	case 'r': rm = v; break;
	case 't': tm = v; break;
	default:
	    return (false);
	}
	cp = strchr(cp, ',');
	if (cp)
	    cp++;
    }
    return (true);
}

void
TextFormat::setPageMargins(TextCoord l, TextCoord r, TextCoord b, TextCoord t)
{
    lm = l;
    rm = r;
    bm = b;
    tm = t;
}

bool
TextFormat::setPageSize(const char* name)
{
    PageSizeInfo* info = PageSizeInfo::getPageSizeByName(name);
    if (info) {
	setPageWidth(info->width() / 25.4);
	setPageHeight(info->height() / 25.4);
	delete info;
	return (true);
    } else
	return (false);
}

void TextFormat::setPageWidth(float pw)		{ physPageWidth = pw; }
void TextFormat::setPageHeight(float ph)		{ physPageHeight = ph; }

void
TextFormat::setModTimeAndDate(time_t t)
{
    struct tm* tm = localtime(&t);
    char buf[30];
    strftime(buf, sizeof (buf), "%X", tm); modTime = buf;
    strftime(buf, sizeof (buf), "%D", tm); modDate = buf;
}
void TextFormat::setModTime(const char* cp)	{ modTime = cp; }
void TextFormat::setModDate(const char* cp)	{ modDate = cp; }

void
TextFormat::beginFormatting(FILE* o)
{
    output = o;
    pageHeight = ICVT(physPageHeight);
    pageWidth = ICVT(physPageWidth);

    /*
     * Open the file w+ so that we can reread the temp file.
     */
    tf = Sys::tmpfile();
    if (tf == NULL)
	fatal(_("Cannot open temporary file: %s"), strerror(errno));

    numcol = fxmax(1,numcol);
    if (pointSize == -1)
	pointSize = inch(numcol > 1 ? "7bp" : "10bp");
    else
	pointSize = fxmax(inch("3bp"), pointSize);
    if (pointSize > inch("18bp"))
	warning(_("point size is unusually large (>18pt)"));
    // read font metrics
    for (FontDictIter iter(*fonts); iter.notDone(); iter++) {
	fxStr emsg;
	TextFont* f = iter.value();
	if (!f->readMetrics(pointSize, useISO8859, emsg))
	    error(_("Font %s: %s"), f->getFamily(), (const char*) emsg);
    }
    outline = fxmax(0L,outline);
    curFont = (*fonts)["Roman"];
    tabWidth = tabStop * curFont->charwidth(' ');

    if (landscape) {
	TextCoord t = pageWidth;
	pageWidth = pageHeight;
	pageHeight = t;
    }
    if (lm+rm >= pageWidth)
	fatal(_("Margin values too large for page; lm %lu rm %lu page width %lu"),
	    lm, rm, pageWidth);
    if (tm+bm >= pageHeight)
	fatal(_("Margin values too large for page; tm %lu bm %lu page height %lu"),
	    tm, bm, pageHeight);

    col_width = (pageWidth - (lm + rm))/numcol;
    if (numcol > 1 || outline)
	col_margin = col_width/COLFRAC;
    else
	col_margin = 0;
    /*
     * TeX's baseline skip is 12pt for
     * 10pt type, we preserve that ratio.
     */
    if (lineHeight <= 0)
	lineHeight = (pointSize * 12L) / 10L;
    workStarted = true;
}

void
TextFormat::endFormatting(void)
{
    emitPrologue();
    /*
     * Now rewind temporary file and write
     * pages to stdout in appropriate order.
     */
    if (reverse) {
	rewind(tf);
	off_t last = (*pageOff)[pageOff->length()-1];
	for (int k = pageNum-firstPageNum; k >= 0; --k) {
	    /* copy remainder in reverse order */
	    off_t next = (off_t) ftell(stdout);
	    Copy_Block((*pageOff)[k],last-1);
	    last = (*pageOff)[k];
	    (*pageOff)[k] = next;
	}
    } else {
	off_t last = ftell(tf);
	rewind(tf);
	Copy_Block(0L, last-1);
    }
    if (fclose(tf))
	fatal(_("Close failure on temporary file: %s"), strerror(errno));
    tf = NULL;
    emitTrailer();
    fflush(output);
    workStarted = false;
}

/* copy bytes b1..b2 to stdout */
void
TextFormat::Copy_Block(off_t b1, off_t b2)
{
    char buf[16*1024];
    for (off_t k = b1; k <= b2; k += sizeof (buf)) {
	size_t cc = (size_t) fxmin(sizeof (buf), (size_t) (b2-k+1));
	fseek(tf, (long) k, SEEK_SET);		// position to desired block
	if (fread(buf, 1, (size_t) cc, tf) != cc)
	    fatal(_("Read error during reverse collation: %s"), strerror(errno));
	if (fwrite(buf, 1, (size_t) cc, output) != cc)
	    fatal(_("Output write error: %s"), strerror(errno));
    }
}

static const char* ISOprologue2 = "\
/reencodeISO{\n\
  dup length dict begin\n\
    {1 index /FID ne {def}{pop pop} ifelse} forall\n\
    /Encoding ISOLatin1Encoding def\n\
    currentdict\n\
  end\n\
}def\n\
/findISO{\n\
  dup /FontType known{\n\
    dup /FontType get 3 ne\n\
    1 index /CharStrings known{\n\
      1 index /CharStrings get /Thorn known\n\
    }{false}ifelse\n\
    and\n\
  }{false}ifelse\n\
}def\n\
";

/*
 * Yech, instead of a single string that we fputs, we
 * break things up into smaller chunks to satisfy braindead
 * compilers...
 */
void
TextFormat::putISOPrologue(void)
{
    fputs("/ISOLatin1Encoding where{pop save true}{false}ifelse\n", output);
    fputs("/ISOLatin1Encoding[\n", output);
    fputs(" /.notdef /.notdef /.notdef /.notdef /.notdef /.notdef\n", output);
    fputs(" /.notdef /.notdef /.notdef /.notdef /.notdef /.notdef\n", output);
    fputs(" /.notdef /.notdef /.notdef /.notdef /.notdef /.notdef\n", output);
    fputs(" /.notdef /.notdef /.notdef /.notdef /.notdef /.notdef\n", output);
    fputs(" /.notdef /.notdef /.notdef /.notdef /.notdef /.notdef\n", output);
    fputs(" /.notdef /.notdef /space /exclam /quotedbl /numbersign\n", output);
    fputs(" /dollar /percent /ampersand /quoteright /parenleft\n", output);
    fputs(" /parenright /asterisk /plus /comma /minus /period\n", output);
    fputs(" /slash /zero /one /two /three /four /five /six /seven\n", output);
    fputs(" /eight /nine /colon /semicolon /less /equal /greater\n", output);
    fputs(" /question /at /A /B /C /D /E /F /G /H /I /J /K /L /M\n", output);
    fputs(" /N /O /P /Q /R /S /T /U /V /W /X /Y /Z /bracketleft\n", output);
    fputs(" /backslash /bracketright /asciicircum /underscore\n", output);
    fputs(" /quoteleft /a /b /c /d /e /f /g /h /i /j /k /l /m\n", output);
    fputs(" /n /o /p /q /r /s /t /u /v /w /x /y /z /braceleft\n", output);
    fputs(" /bar /braceright /asciitilde /guilsinglright /fraction\n", output);
    fputs(" /florin /quotesingle /quotedblleft /guilsinglleft /fi\n", output);
    fputs(" /fl /endash /dagger /daggerdbl /bullet /quotesinglbase\n", output);
    fputs(" /quotedblbase /quotedblright /ellipsis /trademark\n", output);
    fputs(" /perthousand /grave /scaron /circumflex /Scaron /tilde\n", output);
    fputs(" /breve /zcaron /dotaccent /dotlessi /Zcaron /ring\n", output);
    fputs(" /hungarumlaut /ogonek /caron /emdash /space /exclamdown\n", output);
    fputs(" /cent /sterling /currency /yen /brokenbar /section\n", output);
    fputs(" /dieresis /copyright /ordfeminine /guillemotleft\n", output);
    fputs(" /logicalnot /hyphen /registered /macron /degree\n", output);
    fputs(" /plusminus /twosuperior /threesuperior /acute /mu\n", output);
    fputs(" /paragraph /periodcentered /cedilla /onesuperior\n", output);
    fputs(" /ordmasculine /guillemotright /onequarter /onehalf\n", output);
    fputs(" /threequarters /questiondown /Agrave /Aacute\n", output);
    fputs(" /Acircumflex /Atilde /Adieresis /Aring /AE /Ccedilla\n", output);
    fputs(" /Egrave /Eacute /Ecircumflex /Edieresis /Igrave /Iacute\n", output);
    fputs(" /Icircumflex /Idieresis /Eth /Ntilde /Ograve /Oacute\n", output);
    fputs(" /Ocircumflex /Otilde /Odieresis /multiply /Oslash\n", output);
    fputs(" /Ugrave /Uacute /Ucircumflex /Udieresis /Yacute /Thorn\n", output);
    fputs(" /germandbls /agrave /aacute /acircumflex /atilde\n", output);
    fputs(" /adieresis /aring /ae /ccedilla /egrave /eacute\n", output);
    fputs(" /ecircumflex /edieresis /igrave /iacute /icircumflex\n", output);
    fputs(" /idieresis /eth /ntilde /ograve /oacute /ocircumflex\n", output);
    fputs(" /otilde /odieresis /divide /oslash /ugrave /uacute\n", output);
    fputs(" /ucircumflex /udieresis /yacute /thorn /ydieresis\n", output);
    fputs("]def{restore}if\n", output);
    fputs(ISOprologue2, output);
}

static const char* defPrologue = "\
/Cols %u def\n\
/PageWidth %.2f def\n\
/PageHeight %.2f def\n\
/LH %u def\n\
/B{gsave}def\n\
/LN{show}def\n\
/EL{grestore 0 -%d rmoveto}def\n\
/M{0 rmoveto}def\n\
/O{gsave show grestore}def\n\
/LandScape{90 rotate 0 -%ld translate}def\n\
/U{%d mul}def\n\
/UP{U 72 div}def\n\
/S{show grestore 0 -%d rmoveto}def\n\
";

static const char* headerPrologue1 = "\
/InitGaudyHeaders{\n\
  /HeaderY exch def /BarLength exch def\n\
  /ftD /Times-Bold findfont 12 UP scalefont def\n\
  /ftF /Times-Roman findfont 14 UP scalefont def\n\
  /ftP /Helvetica-Bold findfont 30 UP scalefont def\n\
  /fillbox{ % w h x y => -\n\
    moveto 1 index 0 rlineto 0 exch rlineto neg 0 rlineto\n\
    closepath fill\n\
  }def\n\
  /LB{ % x y w h (label) font labelColor boxColor labelPtSize => -\n\
    gsave\n\
    /pts exch UP def /charcolor exch def /boxcolor exch def\n\
    /font exch def /label exch def\n\
    /h exch def /w exch def\n\
    /y exch def /x exch def\n\
    boxcolor setgray w h x y fillbox\n\
    /lines label length def\n\
    /ly y h add h lines pts mul sub 2 div sub pts .85 mul sub def\n\
    font setfont charcolor setgray\n\
    label {\n\
      dup stringwidth pop\n\
      2 div x w 2 div add exch sub ly moveto\n\
      show\n\
      /ly ly pts sub def\n\
    } forall\n\
    grestore\n\
  }def\n\
  /Header{ % (file) [(date)] (page) => -\n\
    /Page exch def /Date exch def /File exch def\n\
    .25 U HeaderY U BarLength .1 sub U .25 U [File] ftF .97 0 14 LB\n\
    .25 U HeaderY .25 add U BarLength .1 sub U .25 U [()] ftF 1 0 14 LB\n\
    .25 U HeaderY U 1 U .5 U Date ftD .7 0 12 LB\n\
    BarLength .75 sub U HeaderY U 1 U .5 U [Page] ftP .7 1 30 LB\n\
    1 1 Cols 1 sub{\n\
      BarLength Cols div mul .19 add U HeaderY U moveto 0 -10 U rlineto stroke\n\
    }for\n\
  }def\n\
}def\n\
";
static const char* headerPrologue2 = "\
/InitNormalHeaders{\n\
  /HeaderY exch def /BarLength exch def\n\
  /ftF /Times-Roman findfont 14 UP scalefont def\n\
  /ftP /Helvetica-Bold findfont 14 UP scalefont def\n\
  /LB{ % x y w h (label) font labelColor labelPtSize => -\n\
    gsave\n\
    /pts exch UP def /charcolor exch def\n\
    /font exch def /label exch def\n\
    /h exch def /w exch def\n\
    /y exch def /x exch def\n\
    /ly y h add h pts sub 2 div sub pts .85 mul sub def\n\
    font setfont charcolor setgray\n\
    label stringwidth pop 2 div x w 2 div add exch sub ly moveto\n\
    label show\n\
    grestore\n\
  }def\n\
  /Header{ % (file) [(date)] (page) => -\n\
    /Page exch def pop /File exch def\n\
    .25 U HeaderY U BarLength 2 div U .5 U File ftF 0 14 LB\n\
    BarLength .75 sub U HeaderY U 1 U .5 U Page ftP 0 14 LB\n\
    1 1 Cols 1 sub{\n\
      BarLength Cols div mul .19 add U HeaderY U moveto 0 -10 U rlineto stroke\n\
    }for\n\
  }def\n\
}def\n\
/InitNullHeaders{/Header{3{pop}repeat}def pop pop}def\n\
";

/*
 * Emit the DSC header comments and prologue.
 */
void
TextFormat::emitPrologue(void)
{
    fputs("%!PS-Adobe-3.0\n", output);
    fprintf(output, "%%%%Creator: HylaFAX TextFormat Class\n");
    fprintf(output, "%%%%Title: %s\n", (const char*) title);
    time_t t = Sys::now();
    fprintf(output, "%%%%CreationDate: %s", ctime(&t));
    char* cp = getlogin();
    fprintf(output, "%%%%For: %s\n", cp ? cp : "");
    fputs("%%Origin: 0 0\n", output);
    fprintf(output, "%%%%BoundingBox: 0 0 %.0f %.0f\n",
	physPageWidth*72, physPageHeight*72);
    fputs("%%Pages: (atend)\n", output);
    fprintf(output, "%%%%PageOrder: %s\n",
	reverse ? "Descend" : "Ascend");
    fprintf(output, "%%%%Orientation: %s\n",
	landscape ? "Landscape" : "Portrait");
    fprintf(output, "%%%%DocumentNeededResources: font");
    FontDictIter iter;
    for (iter = *fonts; iter.notDone(); iter++) {
	TextFont* f = iter.value();
	fprintf(output, " %s", f->getFamily());
    }
    fputc('\n', output);
    if (gaudy) {
	fputs("%%+ font Times-Bold\n", output);
	fputs("%%+ font Times-Roman\n", output);
	fputs("%%+ font Helvetica-Bold\n", output);
    }
    emitClientComments(output);
    fprintf(output, "%%%%EndComments\n");

    fprintf(output, "%%%%BeginProlog\n");
    fputs("/$printdict 50 dict def $printdict begin\n", output);
    if (useISO8859)
	putISOPrologue();
    fprintf(output, defPrologue
	, numcol
	, CVTI(pageWidth - (lm+rm))
	, CVTI(pageHeight - (tm+bm))
	, lineHeight
	, lineHeight
	, pageHeight
	, LUNIT
	, lineHeight
    );
    fputs(headerPrologue1, output);
    fputs(headerPrologue2, output);
    fprintf(output, "%.2f %.2f Init%sHeaders\n"
	, CVTI(pageWidth - (lm+rm))
	, CVTI(pageHeight - tm)
	, (gaudy ? "Gaudy" : headers ? "Normal" : "Null")
    );
    for (iter = *fonts; iter.notDone(); iter++)
	iter.value()->defFont(output, pointSize, useISO8859);
    emitClientPrologue(output);
    fputs("end\n", output);
    fputs("%%EndProlog\n", output);
}
void TextFormat::emitClientComments(FILE*) {}
void TextFormat::emitClientPrologue(FILE*) {}

/*
 * Emit the DSC trailer comments.
 */
void
TextFormat::emitTrailer(void)
{
    fputs("%%Trailer\n", output);
    fprintf(output, "%%%%Pages: %d\n", pageNum - firstPageNum);
    fputs("%%EOF\n", output);
}

void
TextFormat::newPage(void)
{
    x = lm;					// x starts at left margin
    right_x = col_width - col_margin/2;		// right x, 0-relative
    y = pageHeight - tm - lineHeight;		// y at page top
    level = 0;					// string paren level reset
    column = 1;
    boc = true;
    bop = true;
}

void
TextFormat::newCol(void)
{
    x += col_width;				// x, shifted
    right_x += col_width;			// right x, shifted
    y = pageHeight - tm - lineHeight;		// y at page top
    level = 0;
    column++;
    boc = true;
}

static void
putString(FILE* fd, const char* val)
{
    fputc('(', fd);
    for (; *val; val++) {
	u_int c = *val & 0xff;
	if ((c & 0200) == 0) {
	    if (c == '(' || c == ')' || c == '\\')
		fputc('\\', fd);
	    fputc(c, fd);
	} else
	    fprintf(fd, "\\%03o", c);
    }
    fputc(')', fd);
}

void
TextFormat::beginCol(void)
{
    if (column == 1) {				// new page
	if (reverse)  {
	    u_int k = pageNum-firstPageNum;
	    off_t off = (off_t) ftell(tf);
	    if (k < pageOff->length())
		(*pageOff)[k] = off;
	    else
		pageOff->append(off);
	}
	fprintf(tf,"%%%%Page: \"%d\" %d\n", pageNum-firstPageNum+1, pageNum);
	fputs("save $printdict begin\n", tf);
	fprintf(tf, ".05 dup scale\n");
	curFont->setfont(tf);
	if (landscape)
	    fputs("LandScape\n", tf);
	putString(tf, curFile);
	fputc('[', tf);
	putString(tf, modDate);
	putString(tf, modTime);
	fputc(']', tf);
	fprintf(tf, "(%d)Header\n", pageNum);
    }
    fprintf(tf, "%ld %ld moveto\n",x,y);
}

void
TextFormat::beginLine(void)
{
    if (boc)
	beginCol(), boc = false, bop = false;
    fputc('B', tf);
}

void
TextFormat::beginText(void)
{
    fputc('(', tf);
    level++;
}

void
TextFormat::hrMove(TextCoord x)
{
    fprintf(tf, " %ld M ", x);
    xoff += x;
}

void
TextFormat::closeStrings(const char* cmd)
{
    int l = level;
    for (; level > 0; level--)
	fputc(')', tf);
    if (l > 0)
	fputs(cmd, tf);
}

void
TextFormat::beginFile(void)
{
    newPage();				// each file starts on a new page

    bol = true;				// force line start
    bot = true;				// force text start
    xoff = col_width * (column-1);
}

void
TextFormat::endFile(void)
{
    if (!bot)
	endTextLine();
    if (!bol)
	endLine();
    if (!bop) {
	column = numcol;			// force page end action
	endTextCol();
    }
    if (reverse) {
	/*
	 * Normally, beginCol sets the pageOff entry.  Since this is
	 * the last output for this file, we must set it here manually.
	 * If more files are printed, this entry will be overwritten (with
	 * the same value) at the next call to beginCol.
	 */
	off_t off = (off_t) ftell(tf);
	pageOff->append(off);
    }
}

void
TextFormat::formatFile(const char* name)
{
    FILE* fp = fopen(name, "r");
    if (fp != NULL) {
	curFile = name;
	formatFile(fp);
	fclose(fp);
    } else
	error(_("%s: Cannot open file: %s"), name, strerror(errno));
}

void
TextFormat::formatFile(FILE* fp)
{
#if HAS_MMAP
    struct stat sb;
    Sys::fstat(fileno(fp), sb);
    char* addr = (char*)
	mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_SHARED, fileno(fp), 0);
    if (addr == (char*) -1) {		// revert to file reads
#endif
	int c;
	while ((c = getc(fp)) == '\f')	// discard initial form feeds
	    ;
	ungetc(c, fp);
	beginFile();
	format(fp);
	endFile();
#if HAS_MMAP
    } else {
	const char* cp = addr;
	const char* ep = cp + sb.st_size;
	while (cp < ep && *cp == '\f')	// discard initial form feeds
	    cp++;
	beginFile();
	format(cp, ep-cp);
	endFile();
	munmap(addr, (size_t) sb.st_size);
    }
#endif
}

void
TextFormat::format(FILE* fp)
{
    int c;
    while ((c = getc(fp)) != EOF) {
	switch (c) {
	case '\0':			// discard nulls
	    break;
	case '\f':			// form feed
	    if (!bop) {
		endTextCol();
		bol = bot = true;
	    }
	    break;
	case '\n':			// line break
	    if (bol)
		beginLine();
	    if (bot)
		beginText();
	    endTextLine();
	    break;
	case '\r':			// check for overstriking
	    if ((c = getc(fp)) == '\n') {
		ungetc(c,fp);		// collapse \r\n => \n
		break;
	    }
	    closeStrings("O\n");	// do overstriking
	    bot = true;			// start new string
	    break;
	default:
	    TextCoord hm;
	    if (c == '\t' || c == ' ') {
		/*
		 * Coalesce white space into one relative motion.
		 * The offset calculation below is to insure that
		 * 0 means the start of the line (no matter what
		 * column we're in).
		 */
		hm = 0;
		int cc = c;
		TextCoord off = xoff - col_width*(column-1);
		do {
		    if (cc == '\t')
			hm += tabWidth - (off+hm) % tabWidth;
		    else
			hm += curFont->charwidth(' ');
		} while ((cc = getc(fp)) == '\t' || cc == ' ');
		if (cc != EOF)
		    ungetc(cc, fp);
		/*
		 * If the motion is one space's worth, either
		 * a single blank or a tab that causes a blank's
		 * worth of horizontal motion, then force it
		 * to be treated as a blank below.
		 */
		if (hm == curFont->charwidth(' '))
		    c = ' ';
		else
		    c = '\t';
	    } else
		hm = curFont->charwidth(c);
	    if (xoff + hm > right_x) {
		if (!wrapLines)		// discard line overflow
		    break;
		if (c == '\t')		// adjust white space motion
		    hm -= right_x - xoff;
		endTextLine();
	    }
	    if (bol)
		beginLine(), bol = false;
	    if (c == '\t') {		// close open PS string and do motion
		if (hm > 0) {
		    closeStrings("LN");
		    bot = true;		// force new string
		    hrMove(hm);
		}
	    } else {			// append to open PS string
		if (bot)
		    beginText(), bot = false;
		if (040 <= c && c <= 0176) {
		    if (c == '(' || c == ')' || c == '\\')
			fputc('\\',tf);
		    fputc(c,tf);
		} else
		    fprintf(tf, "\\%03o", c & 0xff);
		xoff += hm;
	    }
	    break;
	}
    }
}

void
TextFormat::format(const char* cp, u_int cc)
{
    const char* ep = cp+cc;
    while (cp < ep) {
	int c = *cp++ & 0xff;
	switch (c) {
	case '\0':			// discard nulls
	    break;
	case '\f':			// form feed
	    if (!bop) {
		endTextCol();
		bol = bot = true;
	    }
	    break;
	case '\n':			// line break
	    if (bol)
		beginLine();
	    if (bot)
		beginText();
	    endTextLine();
	    break;
	case '\r':			// check for overstriking
	    if (cp < ep && *cp == '\n')
		break;			// collapse \r\n => \n
	    cp++;			// count character
	    closeStrings("O\n");	// do overstriking
	    bot = true;			// start new string
	    break;
	default:
	    TextCoord hm;
	    if (c == '\t' || c == ' ') {
		/*
		 * Coalesce white space into one relative motion.
		 * The offset calculation below is to insure that
		 * 0 means the start of the line (no matter what
		 * column we're in).
		 */
		hm = 0;
		int cc = c;
		TextCoord off = xoff - col_width*(column-1);
		do {
		    if (cc == '\t')
			hm += tabWidth - (off+hm) % tabWidth;
		    else
			hm += curFont->charwidth(' ');
		} while (cp < ep && ((cc = *cp++) == '\t' || cc == ' '));
		if (cc != '\t' && cc != ' ')
		    cp--;
		/*
		 * If the motion is one space's worth, either
		 * a single blank or a tab that causes a blank's
		 * worth of horizontal motion, then force it
		 * to be treated as a blank below.
		 */
		if (hm == curFont->charwidth(' '))
		    c = ' ';
		else
		    c = '\t';
	    } else
		hm = curFont->charwidth(c);
	    if (xoff + hm > right_x) {
		if (!wrapLines)		// discard line overflow
		    break;
		if (c == '\t')		// adjust white space motion
		    hm -= right_x - xoff;
		endTextLine();
	    }
	    if (bol)
		beginLine(), bol = false;
	    if (c == '\t') {		// close open PS string and do motion
		if (hm > 0) {
		    closeStrings("LN");
		    fprintf(tf, " %ld M ", hm);
		    bot = true;		// force new string
		}
	    } else {			// append to open PS string
		if (bot)
		    beginText(), bot = false;
		if (040 <= c && c <= 0176) {
		    if (c == '(' || c == ')' || c == '\\')
			fputc('\\',tf);
		    fputc(c,tf);
		} else
		    fprintf(tf, "\\%03o", c & 0xff);
	    }
	    xoff += hm;
	    break;
	}
    }
}

static const char* outlineCol = "\n\
gsave\
    %ld setlinewidth\
    newpath %ld %ld moveto\
    %ld %ld rlineto\
    %ld %ld rlineto\
    %ld %ld rlineto\
    closepath stroke \
grestore\n\
";

void
TextFormat::endCol(void)
{
    if (outline > 0) {
	fprintf(tf, outlineCol, outline,
	    x - col_margin, bm,		col_width, 0, 0,
	    pageHeight-bm-tm,		-col_width, 0);
    }
    if (column == numcol) {		// columns filled, start new page
	pageNum++;
	fputs("showpage\nend restore\n", tf);
	flush();
	newPage();
    } else
	newCol();
}

void
TextFormat::endLine(void)
{
    fputs("EL\n", tf);
    if ((y -= lineHeight) < bm)
	endCol();
    xoff = col_width * (column-1);
}

void
TextFormat::endTextCol(void)
{
    closeStrings("LN");
    fputc('\n', tf);
    endCol();
}

void
TextFormat::endTextLine(void)
{
    closeStrings("S\n");
    if ((y -= lineHeight) < bm)
	endCol();
    xoff = col_width * (column-1);
    bol = bot = true;
}

void
TextFormat::reserveVSpace(TextCoord vs)
{
    if (y - vs < bm)
	endCol();
}

void
TextFormat::flush(void)
{
    fflush(tf);
    if (ferror(tf) && errno == ENOSPC)
	fatal(_("Output write error: %s"), strerror(errno));
}

/*
 * Convert a value of the form:
 * 	#.##bp		big point (1in = 72bp)
 * 	#.##cc		cicero (1cc = 12dd)
 * 	#.##cm		centimeter
 * 	#.##dd		didot point (1157dd = 1238pt)
 * 	#.##in		inch
 * 	#.##mm		millimeter (10mm = 1cm)
 * 	#.##pc		pica (1pc = 12pt)
 * 	#.##pt		point (72.27pt = 1in)
 * 	#.##sp		scaled point (65536sp = 1pt)
 * to inches, returning it as the function value.  The case of
 * the dimension name is ignored.  No space is permitted between
 * the number and the dimension.
 */
TextCoord
TextFormat::inch(const char* s)
{
    char* cp;
    double v = strtod(s, &cp);
    if (cp == NULL)
	return (ICVT(0));			// XXX
    if (strncasecmp(cp,"in",2) == 0)		// inches
	;
    else if (strncasecmp(cp,"cm",2) == 0)	// centimeters
	v /= 2.54;
    else if (strncasecmp(cp,"pt",2) == 0)	// points
	v /= 72.27;
    else if (strncasecmp(cp,"cc",2) == 0)	// cicero
	v *= 12.0 * (1238.0 / 1157.0) / 72.27;
    else if (strncasecmp(cp,"dd",2) == 0)	// didot points
	v *= (1238.0 / 1157.0) / 72.27;
    else if (strncasecmp(cp,"mm",2) == 0)	// millimeters
	v /= 25.4;
    else if (strncasecmp(cp,"pc",2) == 0)	// pica
	v *= 12.0 / 72.27;
    else if (strncasecmp(cp,"sp",2) == 0)	// scaled points
	v /= (65536.0 * 72.27);
    else					// big points
	v /= 72.0;
    return ICVT(v);
}

/*
 * Configuration file support.
 */
void
TextFormat::setupConfig()
{
    gaudy	= false;	// emit gaudy headers
    landscape	= false;	// horizontal landscape mode output
    useISO8859	= true;		// use the ISO 8859-1 character encoding
    reverse	= false;	// page reversal flag
    wrapLines	= true;		// wrap/truncate lines
    headers	= true;		// emit page headers

    pointSize = -1;		// font point size in big points
    lm = inch("0.25in");	// left margin
    rm = inch("0.25in");	// right margin
    tm = inch("0.85in");	// top margin
    bm = inch("0.5in");		// bottom margin
    lineHeight = 0;		// inter-line spacing
    numcol = 1;			// number of text columns
    col_margin = 0L;		// inter-column margin
    outline = 0L;		// page and column outline linewidth
    tabStop = 8;		// 8-column tab stop
    setPageSize("default");	// default system page dimensions
}

void
TextFormat::resetConfig()
{
    setupConfig();
}

void TextFormat::configError(const char* ...) {}
void TextFormat::configTrace(const char* ...) {}

#undef streq
#define	streq(a,b)	(strcasecmp(a,b)==0)

bool
TextFormat::setConfigItem(const char* tag, const char* value)
{
    if (streq(tag, "columns"))
	setNumberOfColumns(getNumber(value));
    else if (streq(tag, "pageheaders"))
	setPageHeaders(getBoolean(value));
    else if (streq(tag, "linewrap"))
	setLineWrapping(getBoolean(value));
    else if (streq(tag, "iso8859"))
	setISO8859(getBoolean(value));
    else if (streq(tag, "textfont"))
	setTextFont(value);
    else if (streq(tag, "gaudyheaders"))
	setGaudyHeaders(getBoolean(value));
    else if (streq(tag, "pagemargins"))
	setPageMargins(value);
    else if (streq(tag, "outlinemargin"))
	setOutlineMargin(inch(value));
    else if (streq(tag, "textpointsize"))
	setTextPointSize(inch(value));
    else if (streq(tag, "orientation"))
	setPageOrientation(streq(value, "landscape") ? LANDSCAPE : PORTRAIT);
    else if (streq(tag, "pagesize"))
	setPageSize(value);
    else if (streq(tag, "pagewidth"))
	setPageWidth(atof(value));
    else if (streq(tag, "pageheight"))
	setPageHeight(atof(value));
    else if (streq(tag, "pagecollation"))
	setPageCollation(streq(value, "forward") ? FORWARD : REVERSE);
    else if (streq(tag, "textlineheight"))
	setTextLineHeight(inch(value));
    else if (streq(tag, "tabstop"))
	tabStop = getNumber(value);
    else if (streq(tag, "fontmap"))		// XXX
	TextFont::fontMap = value;
    else if (streq(tag, "fontpath"))
	setFontPath(value);
    else
	return (false);
    return (true);
}

#define	NCHARS	(sizeof (widths) / sizeof (widths[0]))

fxStr TextFont::fontMap = _PATH_FONTMAP;
fxStr TextFont::fontPath = _PATH_AFM;
u_int TextFont::fontID = 0;

TextFont::TextFont(const char* cp) : family(cp)
{
    showproc = fxStr::format("s%u", fontID);
    setproc = fxStr::format("sf%u", fontID);
    fontID++;
}
TextFont::~TextFont() {}

bool
TextFont::decodeFontName(const char* name, fxStr& filename, fxStr& emsg)
{
    struct stat junk;
    fxStr path = fontMap;
    u_int index = path.next(0, ':');
    while (index > 0) {

        /* Newer versions of Ghostscript use "Fontmap.GS"
         * whereas older ones omit the ".GS" extension.
         * If the Fontmap.GS file isn't available default
         * to the older convention.
         */
        filename = path.head(index) | "/" | "Fontmap.GS";
        if (stat(filename, &junk) != 0)
            filename = path.head(index) | "/" | "Fontmap";
        fxStr fontMapFile = filename;

        path.remove(0, index);
        if (path.length() > 0) path.remove(0, 1);
        FILE* fd = Sys::fopen(fontMapFile, "r");
        if (fd != NULL && fontMapFile[0] == '/') {
            fxStr key = name;
            char buf[1024];
	    int aliascount = maxaliases;
            while (fgets(buf, sizeof(buf), fd) != NULL &&
		aliascount > 0) {
                size_t len = strcspn(buf, "%\n");
                if (len == strlen(buf)) {
	            emsg = fxStr::format(
	                _("Warning: %s - line too long."), (const char*)fontMapFile);
	                break;
                }
	        if (len == 0) continue;
                *(buf + len) = '\0';
                char* tmp = buf + strcspn(buf, ") \t");
                *tmp++ = '\0';
                tmp += strspn(tmp, " \t");
                if (strcmp(key, buf + 1) == 0) {
                    //match - now ensure it is the last one in the file
		    // for gs compatibility
                    *(tmp + strcspn(tmp, ") \t;")) = '\0';
                    fxStr val = tmp;
                    while (fgets(buf, sizeof(buf), fd) != NULL) {
                        len = strcspn(buf, "%\n");
                        *(buf + len) = '\0';
                        if (len == strlen(buf)) {
	                    emsg = fxStr::format(
		                _("Warning: %s - line too long."), (const char*) fontMapFile);
	                    break;
                        }
			if (len == 0) continue;
	                tmp = buf + strcspn(buf, ") \t");
		        *tmp++ = '\0';
                        tmp += strspn(tmp, " \t");
                        if (strcmp(key, buf + 1) == 0) {
                            *(tmp + strcspn(tmp, ") \t;")) = '\0';
                            val = tmp;
			}
		    }
                    if (val[0] == '/') {
		        //alias
	                aliascount--;
			val.remove(0);
		        key = val;
	        	fseek(fd, 0L, SEEK_SET);
                    } else {
                        //real file
                        fclose(fd);
			val.remove(0);
			int pos = val.next(0, '.');
			val.remove(pos, val.length() - pos);
			val.append(".afm");
			//move through dirs looking for font
			fxStr fpath = fontPath;
                        int index2 = fpath.next(0, ':');
                        filename = fpath.head(index2) | "/" | val;
                        fpath.remove(0, index2);
    			if (fpath.length() > 0) fpath.remove(0, 1);
			while (stat(filename, &junk) != 0 && index2 > 0) {
                            index2 = fpath.next(0, ':');
                            filename = fpath.head(index2) | "/" | val;
                            fpath.remove(0, index2);
    			    if (fpath.length() > 0) fpath.remove(0, 1);
			}
			bool result = stat(filename, &junk) ? false : true;
			if (!result)
	                    emsg = fxStr::format(
			        _("Warning: %s invalid Fontmap entry - no filename present"), (const char*)val);
                        return result;
                    }
                }
            }
            fclose(fd);
        }
        index = path.next(0, ':');
    }
    //decoding using fontmap has failed
    //now try plain filename with/without afm extension
    path = fontPath;
    index = path.next(0, ':');
    while (index > 0) {
        filename = path.head(index) | "/" | name | ".afm";
        path.remove(0, index);
        if (path.length() > 0) path.remove(0, 1);
        if (stat(filename, &junk) == 0) return true;
	filename.resize(filename.length()-4);	// strip ``.afm''
        if (stat(filename, &junk) == 0) return true;
        index = path.next(0, ':');
    }
    return false;
}

bool
TextFont::findFont(const char* name)
{
    fxStr myname, emsg;
    return decodeFontName(name, myname, emsg);
}

static const char* defISOFont = "\
/%s{/%s findfont\
  findISO{reencodeISO /%s-ISO exch definefont}if\
  %d UP scalefont setfont\
}def\n\
";
static const char* defRegularFont = "\
/%s{/%s findfont %d UP scalefont setfont}def\n\
";

void
TextFont::defFont(FILE* fd, TextCoord ps, bool useISO8859) const
{
    if (useISO8859) {
	fprintf(fd, defISOFont, (const char*) setproc,
	    (const char*) family, (const char*) family, ps/20, ps/20); 
    } else {
	fprintf(fd, defRegularFont, (const char*) setproc,
	    (const char*) family, ps/20);
    }
    fprintf(fd, "/%s{%s show}def\n",
	(const char*) showproc, (const char*) setproc);
}

void
TextFont::setfont(FILE* fd)	const
{
    fprintf(fd, " %s ", (const char*) setproc);
}

TextCoord
TextFont::show(FILE* fd, const char* val, int len) const
{
    TextCoord hm = 0;
    if (len > 0) {
	fprintf(fd, "(");
	do {
	    u_int c = *val++ & 0xff;
	    if ((c & 0200) == 0) {
		if (c == '(' || c == ')' || c == '\\')
		    fputc('\\', fd);
		fputc(c, fd);
	    } else
		fprintf(fd, "\\%03o", c);
	    hm += widths[c];		// Leif Erlingsson <leif@lege.com>
	} while (--len);
	fprintf(fd, ")%s ", (const char*) showproc);
    }
    return (hm);
}

TextCoord
TextFont::show(FILE* fd, const fxStr& s) const
{
    return show(fd, s, s.length());
}

TextCoord
TextFont::strwidth(const char* cp) const
{
    TextCoord w = 0;
    while (*cp)
	w += widths[(unsigned) (*cp++ & 0xff)];	// Leif Erlingsson <leif@lege.com>
    return w;
}

void
TextFont::loadFixedMetrics(TextCoord w)
{
    for (u_int i = 0; i < NCHARS; i++)
	widths[i] = w;
}

bool
TextFont::getAFMLine(FILE* fp, char* buf, int bsize)
{
    if (fgets(buf, bsize, fp) == NULL)
	return (false);
    char* cp = strchr(buf, '\n');
    if (cp == NULL) {			// line too long, skip it
	int c;
	while ((c = getc(fp)) != '\n')	// skip to end of line
	    if (c == EOF)
		return (false);
	cp = buf;			// force line to be skipped
    }
    *cp = '\0';
    return (true);
}

FILE*
TextFont::openAFMFile(fxStr& fontpath)
{
    fxStr emsg;
    if (!decodeFontName(family, fontpath, emsg)) {
	fprintf(stderr,emsg);
        return NULL;
    } 
    return Sys::fopen(fontpath, "r");
}

bool
TextFont::readMetrics(TextCoord ps, bool useISO8859, fxStr& emsg)
{
    fxStr file;
    FILE *fp = openAFMFile(file);
    if (fp == NULL) {
	emsg = fxStr::format(
	    _("%s: Can not open font metrics file; using fixed widths"),
	    (const char*) file);
	loadFixedMetrics(625*ps/1000L);		// NB: use fixed width metrics
	return (false);
    }
    /*
     * Since many ISO-encoded fonts don't include metrics for
     * the higher-order characters we cheat here and use a
     * default metric for those glyphs that were unspecified.
     */
    loadFixedMetrics(useISO8859 ? 625*ps/1000L : 0);

    char buf[1024];
    u_int lineno = 0;
    do {
	if (!getAFMLine(fp, buf, sizeof (buf))) {
	    emsg = fxStr::format(
		_("%s: No glyph metric table located; using fixed widths"),
		(const char*) file);
	    fclose(fp);
	    /*
	     * Next line added by Leif Erlingsson <leif@lege.com> because
	     * otherwise, if the metrics-file has no glyph metric table
	     * and useISO8859 == False, the FixedMetrics will be all 0's!
	     * (I don't know if this does or does not cause any problem.)
	     */
	    loadFixedMetrics(625*ps/1000L);	// NB: use fixed width metrics
	    return (false);
	}
	lineno++;
    } while (strncmp(buf, "StartCharMetrics", 16));
    while (getAFMLine(fp, buf, sizeof (buf)) && strcmp(buf, "EndCharMetrics")) {
	lineno++;
	int ix, w;
	/* read the glyph position and width */
	if (sscanf(buf, "C %d ; WX %d ;", &ix, &w) != 2) {
	    emsg = fxStr::format(_("%s, line %u: format error"),
		(const char*) file, lineno);
	    fclose(fp);
	    return (false);
	}
	if (ix == -1)			// end of unencoded glyphs
	    break;
	/*
	 * Next if-clause added by Leif Erlingsson <leif@lege.com> because
	 * experience has shown most glyph metric table's to be useless
	 * for obtaining character widths of iso8859-1 characters > 127.
	 * The Adobe Helvetica-Oblique metrics-file created Tue Apr 1
	 * 12:54:09 PST 1986 caused bad spacing for eight-bit iso-8859-1
	 * characters, for example.
	 */
	if (ix > 127)
	    w = 625;			// distrust metrics-file for char > 127
	if ((unsigned)ix < NCHARS)
	    widths[ix] = w*ps/1000L;
    }
    fclose(fp);
    return (true);
}
