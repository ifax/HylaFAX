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
    fxStr	fromFaxNumber;	// sender's fax number
    fxStr	fromVoiceNumber;	// sender's voice number
    fxStr	fromLocation;	// sender's geographical location
    fxStr	fromCompany;	// sender's company/institution
    fxStr	regarding;	// fax is regarding...
    fxStr	comments;	// general comments
    fxStr	sender;		// sender's identity
    fxStr	pageCount;	// # pages, not counting cover page
    fxStr	dateFmt;	// strftime format string
    float	pageWidth;	// page width (mm)
    float	pageLength;	// page length (mm)
    int		maxcomments;	// max # of comment lines
    int		maxlencomments; // max length of comment lines

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
    maxlencomments = 35;
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
    while ((c = getopt(argc, argv, "C:D:L:N:V:X:n:t:f:c:p:l:m:z:r:s:v:x:z:")) != -1)
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
	case 'L':			// sender's geographic location
	    fromLocation = optarg;
	    break;
	case 'N':			// sender's fax number
	    fromFaxNumber = optarg;
	    break;
	case 'V':			// sender's voice number
	    fromVoiceNumber = optarg;
	    break;
	case 'X':			// sender's company/institution
	    fromCompany = optarg;
	    break;
	case 'm':			// max # comment lines
	    maxcomments = atoi(optarg);
	    break;
	case 'z':			// max length comment lines
	    maxlencomments = atoi(optarg);
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
	" [-z maxlencomments]"
	" [-r regarding]"
	" [-v to-voice-number]"
	" [-x to-company]"
	" [-C template-file]"
	" [-D date-format]"
	" [-L from-location]"
	" [-N from-fax-number]"
	" [-V from-voice-number]"
	" [-X from-company]"
	" [-s pagesize]"
	" -f from"
	" -n fax-number"
	, (const char*) appName);
}

const char* faxCoverApp::prologue = "\
/wordbreak ( ) def\n\
/linebreak (\\n) def\n\
/doLine {\n\
% <line> <width> <height> <x> <y> doLine <width> <height> <x> <y>\n\
    2 copy moveto 5 -1 roll\n\
    wordbreak\n\
    {\n\
        search {\n\
            dup stringwidth pop currentpoint pop add 7 index 6 index add gt {\n\
                6 3 roll 2 index sub 2 copy moveto 6 3 roll\n\
            } if\n\
            show wordbreak show\n\
        }{\n\
            dup stringwidth pop currentpoint pop add 5 index 4 index add gt {\n\
                3 1 roll 3 index sub 2 copy moveto 3 -1 roll\n\
            } if\n\
            show exit\n\
        } ifelse\n\
    } loop\n\
    2 index sub 2 copy moveto\n\
} def\n\
/BreakIntoLines {\n\
% <width> <height> <x> <y> <text> BreakIntoLines\n\
    linebreak\n\
    {\n\
         search {\n\
             7 3 roll doLine 6 -2 roll\n\
         }{\n\
             5 1 roll doLine exit\n\
         } ifelse\n\
    } loop\n\
    pop pop pop pop\n\
} def\n\
/BreakIntoCommentX {\n\
% <maxlines> <text> BreakIntoCommentX -\n\
    /cbuf (Comment ) def\n\
    0 exch\n\
    linebreak { search { 4 -1 roll 1 add 4 2 roll }{ exch 1 add exit } ifelse } loop\n\
    dup dup 2 add 1 roll\n\
    -1 1 { cbuf exch 7 exch 48 add put cbuf cvn exch def } for\n\
    1 add exch 1 exch { cbuf exch 7 exch 48 add put cbuf cvn () def } for\n\
} def\n\
/XtoCommentsX {\n\
% <X> XtoCommentsX <commentsX>\n\
    3 string cvs (comments) dup length dup 4 1 roll\n\
    2 index length add string dup 0 4 -1 roll\n\
    putinterval dup 4 -2 roll putinterval\n\
} def\n\
/BreakIntoCommentsX {\n\
% <maxlines> <text> BreakIntoCommentsX -\n\
    exch 1 1 3 2 roll\n\
    { XtoCommentsX cvn () def } for\n\
    dup length string copy 0 1 index 0 4 1 roll\n\
    {   linebreak 0 get eq {\n\
            exch dup 0 3 index getinterval 4 -1 roll 1 add dup 5 1 roll\n\
            XtoCommentsX cvn exch def dup length 2 index sub 1 sub\n\
            2 index 1 add exch getinterval exch pop 0\n\
        }{ 1 add } ifelse\n\
        dup MaxLenComments gt {\n\
            exch MaxLenComments 1 sub -1 0 {\n\
                2 copy get wordbreak 0 get eq {\n\
                    mark 4 1 roll\n\
                    {   2 copy 1 add 1 index length 1 index 1 add sub\n\
                        getinterval 5 -1 roll search { 3 -2 roll pop pop } if\n\
                        length MaxLenComments gt { 4 -1 roll exec\n\
                        }{ false } ifelse\n\
                    }\n\
                    { true }\n\
                    5 1 roll linebreak 1 index wordbreak 7 3 roll exec\n\
                    counttomark 1 add 4 roll cleartomark { pop exit } if\n\
                    2 copy 1 add 0 exch getinterval 5 -1 roll\n\
                    1 add dup 6 1 roll XtoCommentsX cvn exch def\n\
                    2 copy 1 add 1 index length 1 index sub getinterval\n\
                    3 -1 roll pop 3 -2 roll 1 add sub exch exit\n\
                } if\n\
                pop\n\
            } for\n\
            exch dup MaxLenComments gt {\n\
                pop dup 0 MaxLenComments getinterval 3 -1 roll\n\
                1 add dup 4 1 roll XtoCommentsX cvn exch def\n\
                dup length MaxLenComments sub\n\
                MaxLenComments exch getinterval 1\n\
            } if\n\
        }if\n\
    } forall\n\
    pop exch 1 add XtoCommentsX cvn exch def\n\
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
    printf("%i dict begin\n", maxcomments*2 + 80);
    printf("%s", prologue);
    emitToDefs(toName, toName != "" ? db->find(toName) : (FaxDBRecord*) NULL);
    printf("/pageWidth %.2f def\n", pageWidth);
    printf("/pageLength %.2f def\n", pageLength);
    emitFromDefs(db->find(sender));
    coverDef("page-count", pageCount);
    emitDateDefs();
    coverDef("regarding", regarding);
    emitCommentDefs();
    printf("/MaxComments %i def\n", maxcomments);
    printf("MaxComments comments BreakIntoCommentX\n");
    printf("/MaxLenComments %i def\n", maxlencomments);
    printf("MaxComments comments BreakIntoCommentsX\n");
    printf("%%%%EndProlog\n");
    printf("%%%%Page: \"1\" 1\n");
    // copy prototype cover page
    char buf[16*1024];
    int n;
    while ((n = read(fd, buf, sizeof (buf))) > 0) 
	fwrite(buf, n, 1, stdout);
    Sys::close(fd);
    printf("\nend\n");
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
     * Fix up new line characters.
     */
    u_int crlf = comments.find(0, "\\n", (u_int) 0);
    while ( crlf < comments.length()) {
        comments.remove(crlf, 2);
        comments.insert('\n', crlf);
        crlf = comments.find(0, "\\n", (u_int) 0);
    }
    coverDef("comments", comments);
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
        struct passwd* pwd = getpwuid(getuid());
        if (!pwd) {
            fxFatal("Can not figure out who you are.");
        }
        char* cp = pwd->pw_dir;
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
    fprintf(stderr, "%s: ", (const char*) appName);
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
