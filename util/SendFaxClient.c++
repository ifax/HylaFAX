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
#include "config.h"
#include "Sys.h"

#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "SendFaxClient.h"
#include "TypeRules.h"
#include "DialRules.h"
#include "Array.h"

struct FileInfo : public fxObj {
    fxStr	name;			// user-specified document file
    fxStr	temp;			// converted temporary file
    fxStr	doc;			// document name on server
    const TypeRule* rule;

    FileInfo();
    FileInfo(const FileInfo& other);
    ~FileInfo();
};
fxDECLARE_ObjArray(FileInfoArray, FileInfo)

struct PollRequest : public fxObj {
    fxStr	sep;
    fxStr	pwd;

    PollRequest();
    PollRequest(const PollRequest& other);
    ~PollRequest();
};
fxDECLARE_ObjArray(PollRequestArray, PollRequest)

SendFaxClient::SendFaxClient()
{
    jobs = new SendFaxJobArray;
    typeRules = NULL;
    dialRules = NULL;
    files = new FileInfoArray;
    polls = new PollRequestArray;
    setup = false;

    setupConfig();
}

SendFaxClient::~SendFaxClient()
{
    if (tmpFile != "")
	Sys::unlink(tmpFile);
    delete typeRules;
    delete dialRules;
    delete polls;
    delete files;
    delete jobs;
}

void SendFaxClient::vprintError(const char* fmt, va_list ap)
    { FaxClient::vprintError(fmt, ap); }
void SendFaxClient::vprintWarning(const char* fmt, va_list ap)
    { FaxClient::vprintWarning(fmt, ap); }
void SendFaxClient::vtraceServer(const char* fmt, va_list ap)
    { FaxClient::vtraceServer(fmt, ap); }

bool
SendFaxClient::prepareForJobSubmissions(fxStr& emsg)
{
    if (senderName == "" && !setupSenderIdentity(from, emsg))
	return (false);
    /*
     * Prepare documents for transmission.
     */
    if (typeRules == NULL) {
	typeRules = TypeRules::read(typeRulesFile);
	if (!typeRules) {
	    emsg = "Unable to setup file typing and conversion rules";
	    return (false);
	}
    }
    typeRules->setVerbose(verbose);
    if (dialRules == NULL) {
	dialRules = new DialStringRules(dialRulesFile);
	dialRules->setVerbose(verbose);
	/*
	 * NB: Not finding a client-side dialrules file is not fatal; it
	 *     used to generate a warning message but so many people were
	 *     confused by this that the message has been removed so I no
	 *     longer have to explain why it's not a problem.
	 */
	(void) dialRules->parse(false);
    } else
	dialRules->setVerbose(verbose);
    /*
     * Lock down job page size information.
     */
    u_int i, n;
    for (i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (job.getPageWidth() != 0 && job.getPageLength() != 0)
	    continue;
	if (!job.setPageSize(job.getPageSize())) {
	    emsg = "Unknown page size " | job.getPageSize();
	    return (false);
	}
    }
    /*
     * NB: Not (currently) smart enough to recognize when
     *     documents need to be reprocessed.  For now we
     *     just assume document conversions are not affected
     *     by job state changes.
     */
    totalPages = 0;
    for (i = 0, n = files->length(); i < n; i++)
	if (!prepareFile((*files)[i], emsg))
	    return (false);
    /*
     * Prepare cover pages.
     */
    for (i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	/*
	 * Convert dialstrings to a displayable format.  This
	 * deals with problems like calling card access codes
	 * getting stuck on the cover sheet and/or displayed in
	 * status messages.
	 */
	job.setExternalNumber(dialRules->displayNumber(job.getDialString()));
	/*
	 * Suppress the cover page if we're just doing a poll;
	 * otherwise, generate a cover sheet for each destination
	 * This done now so that we can be sure everything is ready
	 * to send before we setup a connection to the server.
	 */
	if (job.getAutoCoverPage() && getNumberOfFiles() > 0) {
	    fxStr file;
	    if (!makeCoverPage(job, file, emsg))
		return (false);
	    job.setCoverPageFile(file, true);
	}
    }
    return (setup = true);
}

static fxStr
joinargs(const char* app, const char* av[])
{
    fxStr s(app);
    for (int i = 1; av[i] != NULL; i += 2)
	s.append(fxStr::format(" %s '%s'", av[i], av[i+1]));
    return s;
}

static void
addarg(const char* av[], int& ac, const char* flag, const fxStr& opt)
{
    if (opt != "") {
	av[ac++] = flag;
	av[ac++] = opt;
    }
}

/*
 * Invoke the cover page generation program.
 */
bool
SendFaxClient::makeCoverPage(const SendFaxJob& job, fxStr& file, fxStr& emsg)
{
    char buff[128];
    sprintf(buff, "%s/sndfaxXXXXXX", _PATH_TMP);
    int fd = Sys::mkstemp(buff);
    tmpFile = buff;
    if (fd >= 0) {
#define	MAXARGS	128
	const char* av[MAXARGS];
	int ac = 0;
	const char* cp = strrchr(coverCmd, '/');
	// NB: can't use ?: 'cuz of AIX compiler (XXX)
	if (cp)
	    av[ac++] = cp+1;			// program name
	else
	    av[ac++] = coverCmd;
	addarg(av, ac, "-C", job.getCoverTemplate());
	addarg(av, ac, "-D", dateFormat);
	addarg(av, ac, "-c", job.getCoverComments());
	addarg(av, ac, "-f", senderName);
	addarg(av, ac, "-l", job.getCoverLocation());
	addarg(av, ac, "-n", job.getExternalNumber());
	addarg(av, ac, "-r", job.getCoverRegarding());
	addarg(av, ac, "-s", job.getPageSize());
	addarg(av, ac, "-t", job.getCoverName());
	addarg(av, ac, "-v", job.getCoverVoiceNumber());
	addarg(av, ac, "-x", job.getCoverCompany());
	fxStr pages;
	if (totalPages > 0) {
	    pages = fxStr::format("%u", totalPages);
	    addarg(av, ac, "-p", pages);
	}
	av[ac] = NULL;
	if (verbose)
	    printf("COVER SHEET \"%s\"\n", (const char*) joinargs(coverCmd,av));
	int pfd[2];
	if (pipe(pfd) >= 0) {
	    pid_t pid = fork();
	    switch (pid) {
	    case -1:			// error
		emsg = fxStr::format("Error creating cover sheet; "
		    "could not fork subprocess: %s", strerror(errno));
		Sys::close(pfd[1]);
		break;
	    case 0:			// child, exec command
		if (pfd[1] != STDOUT_FILENO)
		    dup2(pfd[1], STDOUT_FILENO);
		// XXX should close other descriptors
		dup2(STDOUT_FILENO, STDERR_FILENO);
		Sys::execv(coverCmd, (char* const*) av);
		_exit(-1);
		/*NOTREACHED*/
	    default:			// parent, read from pipe and wait
		Sys::close(pfd[1]);
		{ char buf[16*1024];	// XXX for HP C++ compiler
		  int cc;
		  while ((cc = read(pfd[0], buf, sizeof (buf))) > 0)
		      (void) write(fd, buf, cc);
		}
		(void) Sys::close(pfd[0]);
		(void) Sys::close(fd);
		int status;
		if (Sys::waitpid(pid, status) == pid && status == 0) {
		    file = tmpFile;
		    return (true);
		}
		emsg = fxStr::format("Error creating cover sheet; "
		    "command was \"%s\"; exit status %x"
		    , (const char*) joinargs(coverCmd, av)
		    , status
		);
		break;
	    }
	    Sys::close(pfd[0]);
	} else {
	    emsg = fxStr::format("Error creating cover sheet; "
		"unable to create pipe to subprocess: %s", strerror(errno));
	}
#undef MAXARGS
    } else
	emsg = fxStr::format("%s: Can not create temporary file for cover page",
	    (const char*) tmpFile);
    Sys::unlink(tmpFile);
    return (false);
}

SendFaxJob&
SendFaxClient::addJob(void)
{
    u_int ix = jobs->length();
    jobs->resize(ix+1);
    (*jobs)[ix] = proto;
    setup = false;
    return ((*jobs)[ix]);
}
u_int SendFaxClient::getNumberOfJobs() const	{ return jobs->length(); }

SendFaxJob*
SendFaxClient::findJob(const fxStr& number, const fxStr& name)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (job.getDialString() != number)
	    continue;
	if (name != "" && job.getCoverName() == name)
	    return (&job);
    }
    return (NULL);
}

SendFaxJob*
SendFaxClient::findJobByTag(const fxStr& tag)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (job.getJobTag() == tag)
	    return (&job);
    }
    return (NULL);
}

void
SendFaxClient::removeJob(const SendFaxJob& job)
{
    u_int ix = jobs->find(job);
    if (ix != fx_invalidArrayIndex)
	jobs->remove(ix);
}

/*
 * Add a new file to send to each destination.
 */
u_int
SendFaxClient::addFile(const fxStr& filename)
{
    u_int ix = files->length();
    files->resize(ix+1);
    (*files)[ix].name = filename;
    setup = false;
    return (ix);
}

const fxStr&
SendFaxClient::getFileDocument(u_int ix) const
{
    return (ix < files->length() ? (*files)[ix].doc : fxStr::null);
}

u_int SendFaxClient::getNumberOfFiles() const	{ return files->length(); }

u_int
SendFaxClient::findFile(const fxStr& filename) const
{
    for (u_int i = 0, n = files->length(); i < n; i++)
	if ((*files)[i].name == filename)
	    return (i);
    return (fx_invalidArrayIndex);
}

void
SendFaxClient::removeFile(u_int ix)
{
    if (ix < files->length())
	files->remove(ix);
}

void
SendFaxClient::purgeFileConversions()
{
    for (u_int i = 0, n = files->length(); i < n; i++) {
	FileInfo& info = (*files)[i];
	if (info.temp != "" && info.temp != info.name) {
	    Sys::unlink(info.temp);
	    info.temp = "";
	}
    }
}

u_int
SendFaxClient::addPollRequest()
{
    return addPollRequest(fxStr::null, fxStr::null);
}
u_int
SendFaxClient::addPollRequest(const fxStr& sep)
{
    return addPollRequest(sep, fxStr::null);
}
u_int
SendFaxClient::addPollRequest(const fxStr& sep, const fxStr& pwd)
{
    u_int ix = polls->length();
    polls->resize(ix+1);
    (*polls)[ix].sep = sep;
    (*polls)[ix].pwd = pwd;
    setup = false;
    return (ix);
}
u_int SendFaxClient::getNumberOfPollRequests() const { return polls->length(); }

void
SendFaxClient::getPollRequest(u_int ix, fxStr& sep, fxStr& pwd)
{
    if (ix < polls->length()) {
	sep = (*polls)[ix].sep;
	pwd = (*polls)[ix].pwd;
    }
}

void
SendFaxClient::removePollRequest(u_int ix)
{
    if (ix < polls->length())
	polls->remove(ix);
}

/*
 * Submit documents and jobs to the server.
 */
bool
SendFaxClient::submitJobs(fxStr& emsg)
{
    if (!setup) {
	emsg = "Documents not prepared";
	return (false);
    }
    if (!isLoggedIn()) {
	emsg = "Not logged in to server";
	return (false);
    }
    /*
     * Transfer documents to the server.
     */
    if (!sendDocuments(emsg))
	return (false);
    /*
     * Construct jobs and submit them.
     */
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (!job.createJob(*this, emsg))
	    return (false);
	if (!jobSubmit(job.getJobID())) {
	    emsg = getLastResponse();
	    return (false);
	}
	notifyNewJob(job);			// notify client
    }
    return (true);
}

/*
 * Transfer the document files to the server and
 * record the serve-side documents for job submission.
 */
bool
SendFaxClient::sendDocuments(fxStr& emsg)
{
    emsg = "";
    for (u_int i = 0, n = files->length(); i < n; i++) {
	FileInfo& info = (*files)[i];
	int fd = Sys::open(info.temp, O_RDONLY);
	if (fd < 0) {
	    emsg = fxStr::format(info.temp | ": Can not open: %s",
		strerror(errno));
	    return (false);			// XXX
	}
	bool fileSent;
	if (info.rule->getResult() == TypeRule::TIFF) {
	    fileSent = setFormat(FORM_TIFF)
		    && setType(TYPE_I)
		    && sendData(fd, &FaxClient::storeTemp, info.doc, emsg);
	} else {
	    fileSent = setFormat(FORM_PS)
	    	    && setType(TYPE_I)		// XXX TYPE_A???
		    && sendZData(fd, &FaxClient::storeTemp, info.doc, emsg);
	}
	Sys::close(fd);
	if (!fileSent) {
	    if (emsg == "")
		emsg = "Document transfer failed: " | getLastResponse();
	    return (false);
	}
    }
    return (true);
}

/*
 * Default notification handler for when a new job is created.
 */
void
SendFaxClient::notifyNewJob(const SendFaxJob& job)
{
    int nfiles = files->length();
    printf("request id is %s (group id %s) for host %s (%u %s)\n"
	, (const char*) job.getJobID()
	, (const char*) job.getGroupID()
	, (const char*) getHost()
	, nfiles
	, nfiles > 1 ? "files" : "file"
    );
}

/*
 * Configuration file support.
 */
#define	N(a)	(sizeof (a) / sizeof (a[0]))

const SendFaxClient::SF_stringtag SendFaxClient::strings[] = {
{ "typerules",		&SendFaxClient::typeRulesFile,
  FAX_LIBDATA "/" FAX_TYPERULES },
{ "dialrules",		&SendFaxClient::dialRulesFile,
  FAX_LIBDATA "/" FAX_DIALRULES },
{ "covercmd",		&SendFaxClient::coverCmd,
  FAX_CLIENTBIN "/" "faxcover" },
{ "from",		&SendFaxClient::from,			NULL },
{ "dateformat",		&SendFaxClient::dateFormat,		NULL },
};

void
SendFaxClient::setupConfig()
{
    for (int i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    verbose = false;
    delete typeRules, typeRules = NULL;
    delete dialRules, dialRules = NULL;

    proto.setupConfig();
}

void
SendFaxClient::resetConfig()
{
    FaxClient::resetConfig();
    setupConfig();
}

bool
SendFaxClient::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*) strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
    } else if (streq(tag, "verbose")) {
	setVerbose(getBoolean(value));
	FaxClient::setVerbose(getVerbose());		// XXX
    } else if (proto.setConfigItem(tag, value)) {
	;
    } else if (!FaxClient::setConfigItem(tag, value))
	return (false);
    return (true);
}
#undef N

/*
 * Setup the sender's identity.
 */
bool
SendFaxClient::setupSenderIdentity(const fxStr& from, fxStr& emsg)
{
    FaxClient::setupUserIdentity(emsg);		// client identity

    if (from != "") {
	u_int l = from.next(0, '<');
	if (l == from.length()) {
	    l = from.next(0, '(');
	    if (l != from.length()) {		// joe@foobar (Joe Schmo)
		setBlankMailboxes(from.head(l));
		l++, senderName = from.token(l, ')');
	    } else {				// joe
		setBlankMailboxes(from);
		if (from == getUserName())
		    senderName = FaxClient::getSenderName();
		else
		    senderName = "";
	    }
	} else {				// Joe Schmo <joe@foobar>
	    senderName = from.head(l);
	    l++, setBlankMailboxes(from.token(l, '>'));
	}
	if (senderName == "" && getNonBlankMailbox(senderName)) {
	    /*
	     * Mail address, but no "real name"; construct one from
	     * the account name.  Do this by first stripping anything
	     * to the right of an '@' and then stripping any leading
	     * uucp patch (host!host!...!user).
	     */
	    senderName.resize(senderName.next(0, '@'));
	    senderName.remove(0, senderName.nextR(senderName.length(), '!'));
	}

	// strip and leading&trailing white space
	senderName.remove(0, senderName.skip(0, " \t"));
	senderName.resize(senderName.skipR(senderName.length(), " \t"));
    } else {
	senderName = FaxClient::getSenderName();
	setBlankMailboxes(getUserName());
    }
    fxStr mbox;
    if (senderName == "" || !getNonBlankMailbox(mbox)) {
	emsg = "Malformed (null) sender name or mail address";
	return (false);
    } else
	return (true);
}

void SendFaxClient::setFromIdentity(const char* s)	{ from = s; }

/*
 * Assign the specified string to any unspecified email
 * addresses used for notification mail.
 */
void
SendFaxClient::setBlankMailboxes(const fxStr& s)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (job.getMailbox() == "")
	    job.setMailbox(s);
    }
}

/*
 * Return the first non-null mailbox string
 * in the set of jobs.
 */
bool
SendFaxClient::getNonBlankMailbox(fxStr& s)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SendFaxJob& job = (*jobs)[i];
	if (job.getMailbox() != "") {
	    s = job.getMailbox();
	    return (true);
	}
    }
    return (false);
}

/*
 * Process a file submitted for transmission.
 */
bool
SendFaxClient::prepareFile(FileInfo& info, fxStr& emsg)
{
    info.rule = fileType(info.name, emsg);
    if (!info.rule)
	return (false);
    if (info.temp != "" && info.temp != info.name)
	Sys::unlink(info.temp);
    if (info.rule->getCmd() != "") {	// conversion required
    char buff[128];
    sprintf(buff, "%s/sndfaxXXXXXX", _PATH_TMP);
    Sys::mktemp(buff);
    tmpFile = buff;
	/*
	 * XXX **** WARNING **** XXXX
	 *
	 * We need to generate files according to each job's
	 * parameters (page dimensions, resolution, etc.) but
	 * the existing protocol does not support doing this
	 * right so for now we assume all jobs share these
	 * parameters and just use the values from the first
	 * prototype job.
	 */
	fxStr sysCmd = info.rule->getFmtdCmd(info.name, tmpFile,
		proto.getHResolution(), proto.getVResolution(),
		"1", proto.getPageSize());
	if (verbose)
	    printf("CONVERT \"%s\"\n", (const char*) sysCmd);
	if (system(sysCmd) != 0) {
	    Sys::unlink(tmpFile);
	    emsg = fxStr::format("Error converting data; command was \"%s\"",
		(const char*) sysCmd);
	    return (false);
	}
	info.temp = tmpFile;
    } else				// already postscript or tiff
	info.temp = info.name;
    switch (info.rule->getResult()) {
    case TypeRule::TIFF:
	countTIFFPages(info.temp);
	break;
    case TypeRule::POSTSCRIPT:
	estimatePostScriptPages(info.temp);
	break;
    }
    return (true);
}

/*
 * Return a TypeRule for the specified file.
 */
const TypeRule*
SendFaxClient::fileType(const char* filename, fxStr& emsg)
{
    struct stat sb;
    int fd = Sys::open(filename, O_RDONLY);
    if (fd < 0) {
	emsg = fxStr::format("%s: Can not open file", filename);
	return (NULL);
    }
    if (Sys::fstat(fd, sb) < 0) {
	emsg = fxStr::format("%s: Can not stat file", filename);
	Sys::close(fd);
	return (NULL);
    }
    if ((sb.st_mode & S_IFMT) != S_IFREG) {
	emsg = fxStr::format("%s: Not a regular file", filename);
	Sys::close(fd);
	return (NULL);
    }
    char buf[512];
    int cc = Sys::read(fd, buf, sizeof (buf));
    Sys::close(fd);
    if (cc == 0) {
	emsg = fxStr::format("%s: Empty file", filename);
	return (NULL);
    }
    const TypeRule* tr = typeRules->match(buf, cc);
    if (!tr) {
	emsg = fxStr::format("%s: Can not determine file type", filename);
	return (NULL);
    }
    if (tr->getResult() == TypeRule::ERROR) {
	emsg = fxStr::format("%s: ", filename) | tr->getErrMsg();
	return (NULL);
    }
    return tr;   
}

#include "tiffio.h"

/*
 * Count the number of ``pages'' in a TIFF file.
 */
void
SendFaxClient::countTIFFPages(const char* filename)
{
    TIFF* tif = TIFFOpen(filename, "r");
    if (tif) {
	do {
	    totalPages++;
	} while (TIFFReadDirectory(tif));
	TIFFClose(tif);
    }
}

/*
 * Count the number of pages in a PostScript file.
 * We can really only estimate the number as we
 * depend on the DSC comments to figure this out.
 */
void
SendFaxClient::estimatePostScriptPages(const char* filename)
{
    FILE* fd = fopen(filename, "r");
    if (fd != NULL) {
	char line[2048];
	if (fgets(line, sizeof (line)-1, fd) != NULL) {
	    /*
	     * We only consider ``conforming'' PostScript documents.
	     */
	    if (line[0] == '%' && line[1] == '!') {
		int npagecom = 0;	// # %%Page comments
		int npages = 0;		// # pages according to %%Pages comments
		while (fgets(line, sizeof (line)-1, fd) != NULL) {
		    int n;
		    if (strncmp(line, "%%Page:", 7) == 0)
			npagecom++;
		    else if (sscanf(line, "%%%%Pages: %u", &n) == 1)
			npages += n;
		}
		/*
		 * Believe %%Pages comments over counting of %%Page comments.
		 */
		if (npages > 0)
		    totalPages += npages;
		else if (npagecom > 0)
		    totalPages += npagecom;
	    }
	}
	fclose(fd);
    }
}

FileInfo::FileInfo()
{
    rule = NULL;
}
FileInfo::FileInfo(const FileInfo& other)
    : fxObj(other)
    , name(other.name)
    , temp(other.temp)
    , rule(other.rule)
{
}
FileInfo::~FileInfo()
{
    if (temp != name)
	Sys::unlink(temp);
}
fxIMPLEMENT_ObjArray(FileInfoArray, FileInfo)

PollRequest::PollRequest() {}
PollRequest::~PollRequest() {}
PollRequest::PollRequest(const PollRequest& other)
    : fxObj(other)
    , sep(other.sep)
    , pwd(other.pwd)
{}
fxIMPLEMENT_ObjArray(PollRequestArray, PollRequest)
