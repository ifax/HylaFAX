#include "Sys.h"
#include "Trigger.h"
#include "ModemExt.h"
#include "JobExt.h"
#include "FaxRecvInfo.h"
#include "FaxSendInfo.h"
#include "Socket.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
extern "C" {
#include <netinet/in.h>
}

#include "config.h"

bool	verbose = false;

static int
openFIFO(const char* name, int mode)
{
    int fd = open(name, mode|O_NDELAY, 0);
    if (fd == -1) {
	perror(name);
	return (-1);
    }
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NDELAY) < 0) {
	perror("fcntl");
	return (-1);
    }
    return (fd);
}

static void
send(const char* msg, int msgLen)
{
    int fifo = openFIFO("FIFO", O_WRONLY);
    if (fifo > 0) {
	if (write(fifo, msg, msgLen) != msgLen)
	    perror("write");
	close(fifo);
    }
}

static void
printTime(time_t t)
{
    char buf[80];
    strftime(buf, sizeof (buf), "%h %d %T", localtime(&t));
    printf("%s ", buf);
}

static void
printHeader(const TriggerMsgHeader& h)
{
    printTime(h.tstamp);
    if (verbose)
	printf("[ev=%2u, seq#%-3u, len=%3u> ", h.event, h.seqnum, h.length);
    
}

static void
printParams(const Class2Params& params)
{
    printf("<%s, %s, %s, %s, %s>"
	, (params.ln == LN_A4 ? "A4" : params.ln == LN_B4 ? "B4" : "INF")
	, params.verticalResName()
	, params.dataFormatName()
	, params.bitRateName()
	, params.scanlineTimeName()
    );
}

extern	const char* fmtTime(time_t);
static fxStr strTime(time_t t) { return fxStr(fmtTime(t)); }

static const char*
printJob(const TriggerMsgHeader& h, const char* data)
{
    printHeader(h);
    JobExt job;
    data = job.decode(data);
    if (verbose) {
	time_t now = Sys::now();
	printf(
	      "JOB " | job.jobid
	    | " (dest " | job.dest
	    | fxStr::format(" pri %u", job.pri)
	    | " tts " | strTime(job.tts - now)
	    | " killtime " | strTime(job.killtime - now)
	    | "): ");
    } else {
	printf(
	      "JOB " | job.jobid
	    | " (dest " | job.dest
	    | fxStr::format(" pri %u", job.pri)
	    | "): ");
    }
    return (data);
}

static void
printJobEvent(const TriggerMsgHeader& h, const char* data)
{
    (void) printJob(h, data);
    static const char* jobNames[16] = {
	"created",
	"suspended",
	"ready to send",
	"sleeping awaiting time-to-send",
	"marked dead",
	"being processed by scheduler",
	"corpus reaped",
	"activated",
	"rejected",
	"killed",
	"blocked by another job",
	"delayed by time-of-day restriction or similar",
	"parameters altered",
	"timed out",
	"preparation started",
	"preparation finished",
    };
    printf(jobNames[h.event&15]);
}

static void
printSendEvent(const TriggerMsgHeader& h, const char* data)
{
    data = printJob(h, data);
    if (h.event != Trigger::SEND_POLLRCVD) {
	static const char* sendNames[16] = {
	    "SEND FAX: begin attempt",
	    "SEND FAX: call placed (off-hook)",
	    "SEND FAX: connected to remote device",
	    "#%u",
	    "#%u",
	    "#%u",
	    "#%u",
	    "SEND FAX: finished attempt",
	    "SEND FAX: reformat documents because of capabilities mismatch",
	    "SEND FAX: requeue job",
	    "SEND FAX: job completed successfully",
	    "unknown event #11",
	    "unknown event #12",
	    "unknown event #13",
	    "unknown event #14",
	    "unknown event #15"
	};
	FaxSendInfo si;
	si.decode(data);
	switch (h.event) {
	case Trigger::SEND_PAGE:		// page sent
	    printf("SEND FAX: page %u sent in %s (file %s) "
		, si.npages
		, fmtTime(si.time)
		, (const char*) si.qfile
	    );
	    printParams(si.params);
	    break;
	case Trigger::SEND_DOC:			// document sent
	    si.decode(data);
	    printf("SEND FAX: document sent in %s (file %s)"
		, fmtTime(si.time)
		, (const char*) si.qfile
	    );
	    break;
	case Trigger::SEND_POLLDONE:		// polling operation done
	    si.decode(data);
	    printf("SEND FAX: poll op done in %s (file %s)"
		, fmtTime(si.time)
		, (const char*) si.qfile
	    );
	    break;
	default:
	    printf(sendNames[h.event&15], h.event);
	    break;
	}
    } else {
	FaxRecvInfo ri;
	ri.decode(data);
	printf("SEND FAX: recv polled document from %s, %u pages in %s, file %s"
	    , (const char*) ri.sender
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	    , (const char*) ri.qfile
	);
    }
}

static void
printModemEvent(const TriggerMsgHeader& h, const char* data)
{
    printHeader(h);
    ModemExt modem;
    data = modem.decode(data);
    static const char* modemNames[16] = {
	"MODEM %s, assigned to job",
	"MODEM %s, released by job",
	"MODEM %s, marked down",
	"MODEM %s, marked ready",
	"MODEM %s, marked busy",
	"MODEM %s, considered wedged",
	"MODEM %s, in-use by an outbound job",
	"MODEM %s, inbound data call begin",
	"MODEM %s, inbound data call completed",
	"MODEM %s, inbound voice call begin",
	"MODEM %s, inbound voice call completed",
	"#11",
	"unknown event #12",
	"unknown event #13",
	"unknown event #14",
	"unknown event #15"
    };
    switch (h.event) {
    case Trigger::MODEM_CID:
	printf("MODEM %s, caller-id information: %s",
	    (const char*) modem.devID, data);
	break;
    default:
	printf(modemNames[h.event&15], (const char*) modem.devID);
	break;
    }
}

static void
printRecvEvent(const TriggerMsgHeader& h, const char* data)
{
    printHeader(h);
    FaxRecvInfo ri;
    ri.decode(data);
    switch (h.event) {
    case Trigger::RECV_BEGIN:
	printf("RECV FAX: begin call");
	break;
    case Trigger::RECV_END:
	printf("RECV FAX: end call");
	break;
    case Trigger::RECV_START:
	printf("RECV FAX: session started, TSI \"%s\" "
	    , (const char*) ri.sender
	);
	printParams(ri.params);
	break;
    case Trigger::RECV_PAGE:
	printf("RECV FAX: from %s, page %u in %s "
	    , (const char*) ri.sender
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	);
	printParams(ri.params);
	break;
    case Trigger::RECV_DOC:
	printf("RECV FAX: from %s, %u pages in %s, file %s"
	    , (const char*) ri.sender
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	    , (const char*) ri.qfile
	);
	break;
    default:
	printf("unknown event #%u", h.event);
	break;
    }
}

#define	MAX	5

extern "C" bzero(void*, size_t);
static	char fifoName[80];

void
sigINT(...)
{
    unlink(fifoName);
    exit(-1);
}

static void
reapChildren(...)
{
    int s;
    while (waitpid(-1, &s, WNOHANG) != -1)
	;
    signal(SIGCLD, reapChildren);
}

void
doProtocol(const char* trigger)
{
    fd_set rd, wr, ex;
    int fd, n;
    char msg[256];

    sprintf(fifoName, "client/%u", getpid());
    if (Sys::mkfifo(fifoName, 0666) < 0 && errno != EEXIST) {
	perror("mkfifo");
	exit(-1);
    }
    signal(SIGINT, sigINT);
    signal(SIGTERM, sigINT);
    signal(SIGPIPE, SIG_IGN);
    fd = openFIFO(fifoName, CONFIG_OPENFIFO);
    if (fd < 0) {
	unlink(fifoName);
	exit(-1);
    }
    sprintf(msg, "T%s:N%s", fifoName, trigger);
    send(msg, strlen(msg)+1);
    for (;;) {
	FD_ZERO(&rd);
	    FD_SET(fd, &rd);
	    FD_SET(fileno(stdin), &rd);
	FD_ZERO(&wr);
	FD_ZERO(&ex);
	n = select(FD_SETSIZE, &rd, &wr, &ex, 0);
	if (n < 0) {
	    perror("select");
	    break;
	}
	if (n == 0) {
	    printf("timeout\n");
	} else if (n > 0) {
	    char buf[16*1024];
	    int cc;

	    if (FD_ISSET(fd, &rd)) {
		while ((cc = read(fd, buf, sizeof (buf)-1)) > 0) {
		    buf[cc] = '\0';
		    char* bp = &buf[0];
		    do {
			char* cp;
			switch (bp[0]) {
			case 'H':		// HELLO
			case 'T':		// create trigger response
			    cp = strchr(bp, '\0');
			    if (cp > bp) {
				if (cp[-1] == '\n')
				    cp[-1] = '\0';
				if (strncmp(bp, "HELLO", 5) == 0) {
				    printTime(Sys::now());
				    printf(" HELLO: (Server startup)\n");
				    send(msg, strlen(msg)+1);
				} else if (bp[0] == 'T') {
				    printTime(Sys::now());
				    if (bp[1] == '!')
					printf("TRIGGER: Syntax error\n");
				    else
					printf("TRIGGER: new trigger #%s\n",
					    bp+1);
				} else {
				}
			    }
			    bp = cp+1;
			    break;
			case '!':		// trigger event message
			    TriggerMsgHeader h;
			    memcpy(&h, bp, sizeof (h));
			    if (h.length > (&buf[cc] - bp)) {
				// need more data to complete message
			    }
			    switch (h.event>>4) {
			    case JOB_BASE>>4:
				printJobEvent(h, bp+sizeof (h));
				break;
			    case SEND_BASE>>4:
				printSendEvent(h, bp+sizeof (h));
				break;
			    case RECV_BASE>>4:
				printRecvEvent(h, bp+sizeof (h));
				break;
			    case MODEM_BASE>>4:
				printModemEvent(h, bp+sizeof (h));
				break;
			    default:
				printf("Unrecognzied message");
			    }
			    putc('\n', stdout);
			    bp += h.length;
			    break;
			default:
			    printf("Unknown trigger message\n");
			    break;
			}
		    } while (bp < &buf[cc]);
		}
	    }
	    if (FD_ISSET(fileno(stdin), &rd)) {
		if (fgets(buf, sizeof (buf)-1, stdin) == NULL)
		    break;			// connection closed
		char* cp = strchr(buf, '\n');
		if (cp)
		    *cp = '\0';
		if (strcmp(buf, "verbose") == 0) {
		    verbose = !verbose;
		    printf("Verbose %s\n", verbose ? "enabled" : "disabled");
		}
	    }
	} else
	    printf("(n == 0)\n");
	fflush(stdout);
    }
    unlink(fifoName);
}

void
main(int argc, char* argv[])
{

    if (argc != 2 && argc != 3) {
	fprintf(stderr, "usage: %s [-v] trigger-spec\n", argv[0]);
	exit(-1);
    }
    if (argc == 3) {
	verbose = true;
	argc--, argv++;
    }
    if (chdir(FAX_SPOOLDIR) == -1) {
	perror(FAX_SPOOLDIR);
	exit(-1);
    }
    umask(0);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
	perror("socket:");
	exit(-1);
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = 4554;
    if (Socket::bind(s, &sin, sizeof (sin)) < 0) {
	perror("bind:");
	exit(-1);
    }
    signal(SIGCLD, reapChildren);
    listen(s, 5);
    for (;;) {
	Socket::socklen_t sinlen = sizeof (sin);
	int c = Socket::accept(s, &sin, &sinlen);
	if (c < 0) {
	    if (errno == EINTR)
		continue;
	    perror("accept:");
	    exit(-1);
	}
	switch (fork()) {
	case 0:				/* child, process protocol */
	    close(s);
	    if (dup2(c, fileno(stdin)) < 0 || dup2(c, fileno(stdout)) < 0) {
		perror("dup2:");
		exit(-1);
	    }
	    doProtocol(argv[1]);
	    exit(0);
	    /*NOTREACHED*/
	case -1:			/* fork failure */
	    perror("fork:");
	    break;
	default:			/* parent */
	    close(c);
	    break;
	}
    }
}
