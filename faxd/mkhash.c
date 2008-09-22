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
 * Program to generate hash key values for possible
 * items in a HylaFAX job queue description file.
 */
#include "port.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define HASH_DECLARE(h)		unsigned int h = 0;
#define HASH_ITERATE(h,c)	do { unsigned char cc = (c); if (cc != '!') h = 33*h + cc;} while (0)
#define HASH_FINISH(h)		do {} while (0);

#define PRINT_HASH()						\
	printf("%s\n%s\n%s\n", 					\
	"#define HASH_DECLARE(h)	unsigned int h = 0 ",	\
	"#define HASH_ITERATE(h,c)	do { unsigned char cc = (c); if (cc != '!') h = 33*h + cc;} while (0)", \
	"#define HASH_FINISH(h)		do {} while (0);");


void
hash(const char* cp)
{
    char name[80];
    char* xp = name;

    HASH_DECLARE(nh);

    while (*cp) {
	char c = *cp++;
	if (c == '!')
	    *xp++ = '_';
	else
	    *xp++ = toupper(c);
	HASH_ITERATE(nh, c);
    }
    *xp = '\0';
    HASH_FINISH(nh);

    printf("#define	H_%s	%uU\n", name, nh);
}

int
main()
{
    printf("/* THIS FILE WAS AUTOMATICALLY GENERATED, DO NOT EDIT */\n");
    PRINT_HASH();
    hash("external");
    hash("number");
    hash("mailaddr");
    hash("sender");
    hash("jobid");
    hash("jobtag");
    hash("pagehandling");
    hash("modem");
    hash("faxnumber");
    hash("tsi");
    hash("receiver");
    hash("company");
    hash("location");
    hash("voice");
    hash("fromcompany");
    hash("fromlocation");
    hash("fromvoice");
    hash("regarding");
    hash("comments");
    hash("cover");
    hash("client");
    hash("owner");
    hash("groupid");
    hash("signalrate");
    hash("dataformat");
    hash("jobtype");
    hash("tagline");
    hash("subaddr");
    hash("passwd");
    hash("state");
    hash("npages");
    hash("totpages");
    hash("skippages");
    hash("ntries");
    hash("ndials");
    hash("totdials");
    hash("maxdials");
    hash("tottries");
    hash("maxtries");
    hash("pagewidth");
    hash("resolution");
    hash("pagelength");
    hash("priority");
    hash("schedpri");
    hash("minbr");
    hash("desiredbr");
    hash("desiredst");
    hash("desiredec");
    hash("desireddf");
    hash("desiredtl");
    hash("useccover");
    hash("usexvres");
    hash("tts");
    hash("killtime");
    hash("retrytime");
    hash("poll");
    hash("tiff");
    hash("pdf");
    hash("!pdf");
    hash("!tiff");
    hash("postscript");
    hash("!postscript");
    hash("pcl");
    hash("!pcl");
    hash("fax");
    hash("data");
    hash("!data");
    hash("page");
    hash("!page");
    hash("notify");
    hash("pagechop");
    hash("chopthreshold");
    hash("csi");
    hash("nsf");
    hash("status");
    hash("statuscode");
    hash("returned");
    hash("doneop");
    hash("commid");
    hash("pagerange");
    return (0);
}
