#! /bin/sh
#	$Id$
#
# HylaFAX Facsimile Software
#
# Copyright (c) 1990-1996 Sam Leffler
# Copyright (c) 1991-1996 Silicon Graphics, Inc.
# 
# Permission to use, copy, modify, distribute, and sell this software and 
# its documentation for any purpose is hereby granted without fee, provided
# that (i) the above copyright notices and this permission notice appear in
# all copies of the software and related documentation, and (ii) the names of
# Sam Leffler and Silicon Graphics may not be used in any advertising or
# publicity relating to the software without the specific, prior written
# permission of Sam Leffler and Silicon Graphics.
# 
# THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
# EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
# WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
# 
# IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
# ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
# OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
# LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
# OF THIS SOFTWARE.
#

#
# Awk support program for notify shell script.  This
# stuff is broken out into a separate file to avoid
# overflowing the exec arg list on some systems like SCO.
#

function printItem(fmt, tag, value)
{
    printf "%14s: " fmt "\n", tag, value;
}

function printBanner(banner)
{
    print "";
    print "    ---- " banner " ----";
    print "";
}

function docType(s)
{
    if (match(s, "\.cover"))
	return "PostScript cover page";
    else if (match(s, "\.ps"))
	return "PostScript";
    else if (match(s, "\.tif"))
	return "TIFF";
    else if (match(s, "\.pdf"))
	return "PDF";
    else if (match(s, "\.pcl"))
	return "PCL";
    else
	return "Unknown document type";
}

#
# Construct a return-to-sender message.
#
function returnToSender()
{
    printBanner("Unsent job status");
    printItem("%s", "Destination", number);
    printItem("%s", "JobID", jobid);
    printItem("%s", "GroupID", groupid);
    printItem("%s", "Sender", sender);
    printItem("%s", "Mailaddr", mailaddr);
    if (commid != "")
	printItem("%s", "CommID", commid);
    if (modem != "any")
	printItem("%s", "Modem", modem);
    printItem("%s", "Submitted From", client);
    if (jobType == "facsimile") {
	printItem("%u (mm)", "Page Width", pagewidth);
	printItem("%.0f (mm)", "Page Length", pagelength);
	printItem("%.0f (lpi)", "Resolution", resolution);
    }
    printItem("%s", "Status", status == "" ? "  (nothing available)" : status);
    printItem("%u (exchanges with remote device)", "Dialogs", tottries);
    printItem("%u (consecutive failed calls to destination)", "Dials", ndials);
    printItem("%u (total phone calls placed)", "Calls", totdials);
    if (jobType == "facsimile") {
	printItem("%u (pages transmitted)", "Pages", npages);
	printItem("%u (total pages to transmit)", "TotPages", totpages);
	printItem("%u (attempts to send current page)", "Attempts", ntries);
	printItem("%u (directory of next page to send)", "Dirnum", dirnum);
	if (nfiles > 0) {
	    printBanner("Documents submitted for transmission");
	    print "The following documents were submitted for transmission and are";
	    print "available on the server for reuse until they are automatically";
	    print "purged when this job is " doneop "d.  Documents may also be manually";
	    print "removed using the faxrm command; consult faxrm(1) for information.";
	    print ""
	    printf "%-20s %8s %s\n", "Filename", "Size", "Type";
	    for (i = 0; i < nfiles; i++) {
		"wc -c " files[i] | getline;
		printf "%-20s %8d %s\n", files[i], $1, docType(files[i]);
		close("wc -c " files[i]);
	     }
	}
    } else if (jobType == "pager") {
	if (npins != 0) {
	    printBanner("Unsent pages submitted for transmission");
	    for (i = 0; i < npins; i++)
		printf "%15s\n",  "PIN " pins[i];
	}
	if (nfiles != 0) {
	    printBanner("Message text");
	    while ((getline <files[0]) > 0)
		print $0;
	    close(files[0]);
	}    
    }
}

function returnTranscript()
{
    printBanner("Transcript of session follows");
    comFile = "log/c" commid;
    if ((getline <comFile) > 0) {
	do {
	    if (index($0, "-- data") == 0)
		print $0
	} while ((getline <comFile) > 0);
	close(comFile);
    } else {
	printf "    No transcript available";
	if (commid != "")
	    printf "(CommID c" commid ")";
	print ".";
    }
}

function printStatus(s)
{
    if (s == "")
	print "<no reason recorded>";
    else
	print s
}

function putHeaders(subject)
{
    print "To: " mailaddr;
    print "Subject: " subject;
    print "";
    printf "Your " jobType " job to " number;
}

BEGIN		{ nfiles = 0;
		  npins = 0;
		  pagewidth = 0;
		  pagelength = 0;
		  resolution = 0;
		  jobType = "facsimile";
		  signalrate = "unknown";
		  dataformat = "unknown";
		  doneop = "default";
                pagernum = "unknown";
		  commid = "";
		}
/^jobid/	{ jobid = $2; }
/^groupid/	{ groupid = $2; }
/^state/	{ state = $2+0; }
/^doneop/	{ doneop = $2; }
/^number/	{ number = $2; }
/^external/	{ number = $2; }		# override unprocessed number
/^sender/	{ sender = $2; }
/^mailaddr/	{ mailaddr = $2; }
/^jobtag/	{ jobtag = $2; }
/^jobtype/	{ jobType = $2; }
/^status/	{ status = $0; sub("status:", "", status);
		  if (status ~ /\\$/) {
		      sub("\\\\$", "\n", status);
		      while (getline > 0) {
			  status = status $0;
			  sub("\\\\$", "\n", status);
			  if ($0 !~ /\\$/)
			      break;
		      }
		  }
		}
/^resolution/	{ resolution = $2; }
/^npages/	{ npages = $2; }
/^totpages/	{ totpages = $2; }
/^dirnum/	{ dirnum = $2; }
/^commid/	{ commid = $2; }
/^ntries/	{ ntries = $2; }
/^ndials/	{ ndials = $2; }
/^pagewidth/	{ pagewidth = $2; }
/^pagelength/	{ pagelength = $2; }
/^signalrate/	{ signalrate = $2; }
/^dataformat/	{ dataformat = $2; }
/^modem/	{ modem = $2; }
/^totdials/	{ totdials = $2; }
/^tottries/	{ tottries = $2; }
/^client/	{ client = $2; }
/^[!]*post/	{ files[nfiles++] = $4; }
/^[!]*tiff/	{ files[nfiles++] = $4; }
/^[!]*pcl/	{ files[nfiles++] = $4; }
/^page:/	{ pins[npins++] = $4; }
/^[!]page:/   { pagernum = $4; }
/^data:/	{ files[nfiles++] = $4; }
/^poll/		{ poll = " -p"; }
END {
    if (jobtag == "")
	jobtag = jobType " job " jobid;;
    if (doneop == "default")
	doneop = "remove";
    if (jobType == "pager")
      number = pagernum;
    if (why == "done") {
	putHeaders(jobtag " to " number " completed");
	print " was completed successfully.";
	print "";
	if (jobType == "facsimile") {
	    printItem("%u", "Pages", npages);
	    if (resolution == 196)
		printItem("%s", "Quality", "Fine");
	    else
		printItem("%s", "Quality", "Normal");
	    printItem("%u (mm)", "Page Width", pagewidth);
	    printItem("%.0f (mm)", "Page Length", pagelength);
	    printItem("%s", "Signal Rate", signalrate);
	    printItem("%s", "Data Format", dataformat);
	}
	if (tottries != 1)
	    printItem("%s (exchanges with remote device)", "Dialogs", tottries);
	if (totdials != 1)
	    printItem("%s (total phone calls placed)", "Calls", totdials);
	if (modem != "any")
	    printItem("%s", "Modem", modem);
	printItem("%s", "Submitted From", client);
	printItem("%s", "JobID", jobid);
	printItem("%s", "GroupID", groupid);
	printItem("%s", "CommID", "c" commid);
	printf "\nProcessing time was " jobTime ".\n";
	if (status != "") {
	    print "  Additional information:\n    " status;
	    returnTranscript();
	}
    } else if (why == "failed") {
	putHeaders(jobtag " to " number " failed");
	printf " failed because:\n    ";
	printStatus(status);
	returnTranscript();
	returnToSender();
    } else if (why == "rejected") {
	putHeaders(jobtag " to " number " failed");
	printf " was rejected because:\n    ";
	printStatus(status);
	returnToSender();
    } else if (why == "blocked") {
	putHeaders(jobtag " to " number " blocked");
	printf " is delayed in the scheduling queues because:\n    ";
	printStatus(status);
	print "";
	print "The job will be processed as soon as possible."
    } else if (why == "requeued") {
	putHeaders(jobtag " to " number " requeued");
	printf " was not sent because:\n    ";
	printStatus(status);
	print "";
	print "The job will be retried at " nextTry "."
	returnTranscript();
    } else if (why == "removed" || why == "killed") {
	putHeaders(jobtag " to " number " removed from queue");
	print " was deleted from the queue.";
	if (why == "killed")
	    returnToSender();
    } else if (why == "timedout") {
	putHeaders(jobtag " to " number " failed");
	print " could not be completed before the appointed deadline.";
	returnToSender();
    } else if (why == "format_failed") {
	putHeaders(jobtag " to " number " failed");
	print " was not sent because document conversion"
	print "to facsimile failed.  The output from the converter program was:\n";
	print status "\n";
	printf "Check any PostScript documents for non-standard fonts %s.\n",
	    "and invalid constructs";
	returnToSender();
    } else if (why == "no_formatter") {
	putHeaders(jobtag " to " number " failed");
	print " was not sent because";
	print "the document conversion script was not found.";
	returnToSender();
    } else if (match(why, "poll_*")) {
	putHeaders("Notice about " jobtag);
	printf ", a polling request,\ncould not be completed because ";
	if (why == "poll_rejected")
	    print "the remote side rejected your request.";
	else if (why == "poll_no_document")
	    print "no document was available for retrieval.";
	else if (why == "poll_failed")
	    print "an unspecified problem occurred.";
	print "";
	printf "Processing time was %s.\n", jobTime;
	returnTranscript();
    } else {
	putHeaders("Notice about " jobtag);
	print " had something happen to it."
	print "Unfortunately, the notification script was invoked",
	    "with an unknown reason"
	print "so the rest of this message is for debugging:\n";
	print "why: " why;
	print "jobTime: " jobTime;
	print "nextTry: " nextTry;
	print  "";
	print "This should not happen, please report it to your administrator.";
	returnTranscript();
	returnToSender();
    }
}
