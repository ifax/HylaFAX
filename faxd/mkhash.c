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

int	bits[16];
int	bound;
int	collisions;

void
hash(const char* cp)
{
    char name[80];
    char* xp = name;
    unsigned int h = 0;
    while (*cp) {
	if (*cp == '!')
	    *xp++ = '_';
	else
	    *xp++ = toupper(*cp);
	h += h^*cp++;
    }
    *xp = '\0';
    h %= bound;
    printf("#define	H_%s	%u", name, h);
    if (bits[h/32] & (1<<(h%32))) {
	collisions++;
	printf("	/* collision */");
    }
    putchar('\n');
    bits[h/32] |= 1<<(h%32);
}

int
main()
{
    bound = 226;
    memset(bits, 0, sizeof (bits));
    collisions = 0;
    printf("/* THIS FILE WAS AUTOMATICALLY GENERATED, DO NOT EDIT */\n");
    printf("#define	HASH(x)	((x) %% %u)\n", bound);
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
    hash("returned");
    hash("doneop");
    hash("commid");
    printf("/* %u total collisions */\n", collisions);
    return (0);
}
