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
#include "StrArray.h"
#include "FaxDB.h"
#include "config.h"
#include "PageSize.h"
#include "Sys.h"

#include <stdarg.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/file.h>

class faxCoverApp {
private:
    fxStr	appName;	// for error messages
    fxStr	cover;		// prototype cover sheet
    fxStr	dbName;		// .faxdb filename
    FaxDB*	db;		// fax machine database
    fxStr	toName;		// to person's name
    fxStr	toFaxNumber;	// to's fax number
    fxStr	toVoiceNumber;	// to's voice number
    fxStr	toLocation;	// to's geographical location
    fxStr	toCompany;	// to's company/institution
    fxStr	regarding;	// fax is regarding...
    fxStr	comments;	// general comments
    fxStr	sender;		// sender's identity
    fxStr	pageCount;	// # pages, not counting cover page
    fxStr	dateFmt;	// strftime format string
    float	pageWidth;	// page width (mm)
    float	pageLength;	// page length (mm)
    int		maxcomments;	// max # of comment lines

    static const char* prologue;

    fxStr tildeExpand(const fxStr& filename);
    void setupPageSize(const char* name);
    void emitToDefs(const char* to, FaxDBRecord* rec);
    void emitFromDefs(FaxDBRecord* rec);
    void emitCommentDefs();
    void emitDateDefs();
    void coverDef(const char* tag, const char* value);
    void makeCoverSheet();
    void usage();
    void printError(const char* va_alist ...);
public:
    faxCoverApp();
    ~faxCoverApp();

    void initialize(int argc, char** argv);
    void open();
};

faxCoverApp::faxCoverApp()
    : cover(FAX_COVER)
    , dbName("~/.faxdb")
    , dateFmt("%a %b %d %Y, %H:%M %Z")
{
    db = 0;
    maxcomments = 20;
}

faxCoverApp::~faxCoverApp()
{
}

void
faxCoverApp::initialize(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;

    appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    const char* cp = getenv("FAXCOVER");
    if (cp && *cp)
	cover = cp;

    setupPageSize("default");
    while ((c = getopt(argc, argv, "C:D:n:t:f:c:p:l:m:r:s:v:x:")) != -1)
	switch (c) {
	case 's':			// page size
	    setupPageSize(optarg);
	    break;
	case 'C':			// cover sheet
	    cover = optarg;
	    break;
	case 'D':			// date format string
	    dateFmt = optarg;
	    break;
	case 'm':			// max # comment lines
	    maxcomments = atoi(optarg);
	    break;
	case 'n':			// fax number
	    toFaxNumber = optarg;
	    break;
	case 'r':			// regarding
	    regarding = optarg;
	    break;
	case 't':			// to identity
	    toName = optarg;
	    break;
	case 'f':			// from identity
	    sender = optarg;
	    break;
	case 'c':			// comments
	    comments = optarg;
	    break;
	case 'p':			// page count
	    pageCount = optarg;
	    break;
	case 'l':			// to's location
	    toLocation = optarg;
	    break;
	case 'v':			// to's voice phone number
	    toVoiceNumber = optarg;
	    break;
	case 'x':			// to's company
	    toCompany = optarg;
	    break;
	case '?':
	    usage();
	}
    if (sender == "" || toFaxNumber == "")
	usage();
}

extern void fxFatal(const char* fmt, ...);

void
faxCoverApp::setupPageSize(const char* name)
{
    PageSizeInfo* info = PageSizeInfo::getPageSizeByName(name);
    if (!info)
	fxFatal("Unknown page size \"%s\"", name);
    pageWidth = info->width();
    pageLength = info->height();
    delete info;
}

void
faxCoverApp::open()
{
    if (!db)
	db = new FaxDB(tildeExpand(dbName));
    makeCoverSheet();
}

void
faxCoverApp::usage()
{
    fxFatal("usage: %s"
	" [-t to]"
	" [-c comments]"
	" [-p #pages]"
	" [-l to-location]"
	" [-m maxcomments]"
	" [-r regarding]"
	" [-v to-voice-number]"
	" [-x to-company]"
	" [-C template-file]"
	" [-D date-format]"
	" [-s pagesize]"
	" -f from"
	" -n fax-number"
	, (char*) appName);
}

const char* faxCoverApp::prologue = "\
/wordbreak ( ) def\n\
/BreakIntoLines\n\
{ /proc exch def\n\
  /linewidth exch def\n\
  /textstring exch def\n\
  /breakwidth wordbreak stringwidth pop def\n\
  /curwidth 0 def\n\
  /lastwordbreak 0 def\n\
  /startchar 0 def\n\
  /restoftext textstring def\n\
  { restoftext wordbreak search\n\
    { /nextword exch def pop\n\
      /restoftext exch def\n\
      /wordwidth nextword stringwidth pop def\n\
      curwidth wordwidth add linewidth gt\n\
      { textstring startchar\n\
	lastwordbreak startchar sub\n\
	getinterval proc\n\
	/startchar lastwordbreak def\n\
	/curwidth wordwidth breakwidth add def }\n\
      { /curwidth curwidth wordwidth add\n\
	breakwidth add def\n\
      } ifelse\n\
      /lastwordbreak lastwordbreak\n\
      nextword length add 1 add def\n\
    }\n\
    { pop exit }\n\
    ifelse\n\
  } loop\n\
  /lastchar textstring length def\n\
  textstring startchar lastchar startchar sub\n\
  getinterval proc\n\
} def\n\
";

void
faxCoverApp::makeCoverSheet()
{
    int fd;
    if (cover.length() > 0 && cover[0] != '/') {
	fd = Sys::open(tildeExpand("~/" | cover), O_RDONLY);
	if (fd < 0)
	    fd = Sys::open(fxStr(FAX_LIBDATA) | "/" | cover, O_RDONLY);
    } else
	fd = Sys::open(cover, O_RDONLY);
    if (fd < 0) {
	printError( "Could not locate prototype cover sheet \"%s\"",
	    (const char*) cover);
	return;
    }
    printf("%%!PS-Adobe-2.0 EPSF-2.0\n");
    printf("%%%%Creator: faxcover\n");
    printf("%%%%Title: HylaFAX Cover Sheet\n");
    time_t t = time(0);
    printf("%%%%CreationDate: %s", ctime(&t));
    printf("%%%%Origin: 0 0\n");
    printf("%%%%BoundingBox: 0 0 %.0f %.0f\n",
	(pageWidth/25.4)*72, (pageLength/25.4)*72);
    printf("%%%%Pages: 1 +1\n");
    printf("%%%%EndComments\n");
    printf("%%%%BeginProlog\n");
    printf("100 dict begin\n");
    printf("%s", prologue);
    emitToDefs(toName, toName != "" ? db->find(toName) : (FaxDBRecord*) NULL);
    printf("/pageWidth %.2f def\n", pageWidth);
    printf("/pageLength %.2f def\n", pageLength);
    emitFromDefs(db->find(sender));
    coverDef("page-count", pageCount);
    emitDateDefs();
    coverDef("regarding", regarding);
    emitCommentDefs();
    printf("%%%%EndProlog\n");
    printf("%%%%Page: \"1\" 1\n");
    // copy prototype cover page
    char buf[16*1024];
    int n;
    while ((n = read(fd, buf, sizeof (buf))) > 0) 
	fwrite(buf, n, 1, stdout);
    Sys::close(fd);
    printf("end\n");
}

void
faxCoverApp::emitToDefs(const char* to, FaxDBRecord* rec)
{
    if (rec) {
	to = rec->find(FaxDB::nameKey);
	if (toCompany == "")
	    toCompany = rec->find("Company");
	if (toLocation == "")
	    toLocation = rec->find("Location");
	if (toVoiceNumber == "") {
	    toVoiceNumber = rec->find("Voice-Number");
	    if (toVoiceNumber != "") {
		fxStr areaCode(rec->find("Area-Code"));
		if (areaCode != "")
		    toVoiceNumber.insert("1-" | areaCode | "-");
	    }
	}
    }
    coverDef("to",		to);
    coverDef("to-company",	toCompany);
    coverDef("to-location",	toLocation);
    coverDef("to-voice-number",	toVoiceNumber);
    coverDef("to-fax-number",	toFaxNumber);
}

void
faxCoverApp::emitFromDefs(FaxDBRecord* rec)
{
    fxStr fromCompany;
    fxStr fromLocation;
    fxStr fromFaxNumber;
    fxStr fromVoiceNumber;

    if (rec) {
	fromCompany = rec->find("Company");
	fromLocation = rec->find("Location");
	fromFaxNumber = rec->find(FaxDB::numberKey);
	fromVoiceNumber = rec->find("Voice-Number");
	fxStr areaCode(rec->find("Area-Code"));
	if (areaCode != "") {
	    if (fromFaxNumber != "")
		fromFaxNumber.insert("1-" | areaCode | "-");
	    if (fromVoiceNumber != "")
		fromVoiceNumber.insert("1-" | areaCode | "-");
	}
    }
    coverDef("from",		sender);
    coverDef("from-fax-number",	fromFaxNumber);
    coverDef("from-voice-number",fromVoiceNumber);
    coverDef("from-company",	fromCompany);
    coverDef("from-location",	fromLocation);
}

void
faxCoverApp::emitCommentDefs()
{
    /*
     * Break comment string into multiple lines (if needed).
     */
    coverDef("comments", comments);
    int line = 1;
    while (comments.length() > 0) {
	// strip leading white space
	while (comments.length() > 0 && isspace(comments[0]))
	    comments.remove(0);
	int len = fxmin(comments.length(), (u_int) 35);
	if (len == 35 && !isspace(comments[len-1])) {// break on word boundary
	    int l = len-1;
	    for (; l > 1 && !isspace(comments[l-1]); l--)
		;
	    if (l > 0)
		len = l;
	    // otherwise, break in the middle of this really long word
	}
      int tot = comments.length();
      u_int crlf = comments.find(0, "\\n", (u_int) 0);
      if ( crlf < comments.length() && crlf < len ) {
              len = crlf;
      }
	fxStr num(line, "%u");
	coverDef("comments" | num, comments.cut(0,len));
      if ( crlf < tot && comments.length() >= 2 ) {
              comments.cut(0, 2);
      }
	line++;
    }
    for (; line < maxcomments; line++) {
	fxStr num(line, "%u");
	coverDef("comments" | num, "");
    }
}

void
faxCoverApp::emitDateDefs()
{
    time_t t = time(0);
    char date[128];
    strftime(date, sizeof (date), dateFmt, localtime(&t));
    coverDef("todays-date", date);
}

void
faxCoverApp::coverDef(const char* tag, const char* value)
{
    printf("/%s (", tag);
    for (const char* cp = value; *cp; cp++) {
	if (*cp == '(' || *cp == ')' || *cp == '\\')
	    putchar('\\');
	putchar(*cp);
    }
    printf(") def\n");
}

fxStr
faxCoverApp::tildeExpand(const fxStr& filename)
{
    fxStr path(filename);
    if (filename.length() > 1 && filename[0] == '~') {
	path.remove(0);
	char* cp = getenv("HOME");
	if (!cp || *cp == '\0') {
	    struct passwd* pwd = getpwuid(getuid());
	    if (!pwd)
		fxFatal("Can not figure out who you are.");
	    cp = pwd->pw_dir;
	}
	path.insert(cp);
    }
    return (path);
}

void
faxCoverApp::printError(const char* va_alist ...)
#define	fmt va_alist
{
    va_list ap;
    va_start(ap, va_alist);
    fprintf(stderr, "%s: ", (char*) appName);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ".\n");
}
#undef fmt

int
main(int argc, char** argv)
{
    faxCoverApp app;
    app.initialize(argc, argv);
    app.open();
    return 0;
}
