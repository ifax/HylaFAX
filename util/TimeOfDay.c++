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
#include "TimeOfDay.h"
#include <ctype.h>

class Sys {
public:
    static struct tm* localtime(time_t& t)	{ return ::localtime(&t); }
};

#define	BIT(x)	(1<<(x))

TimeOfDay::TimeOfDay() : tod(0xff, 0, 24*60)	// any day, any time
{
}
TimeOfDay::TimeOfDay(const TimeOfDay& other) : tod(other.tod)
{
    for (_tod* t = other.tod.next; t; t = t->next)
	add(t->days, t->start, t->end);
}
TimeOfDay::~TimeOfDay()
{
    reset();
}

void
TimeOfDay::reset()
{
    if (tod.days != 0xff) {
	_tod* next;
	for (_tod* t = tod.next; t; t = next) {
	    next = t->next;
	    delete t;
	}
	tod.days = 0xff;
	tod.start = 0;
	tod.end = 24*60;
	tod.next = NULL;
    }
}

void
TimeOfDay::add(int days, time_t start, time_t end)
{
    if (tod.days == 0xff) {	// NB: 0xff means initialized by constructure
	tod.days = days;
	tod.start = start;
	tod.end = end;
    } else {
	_tod* t = new _tod(days, start, end);
	_tod** tpp;
	for (tpp = &tod.next; *tpp; tpp = &(*tpp)->next)
	    ;
	*tpp = t;
    }
}

/*
 * Parse time-to-send specification string and generate
 * an internal format that's easy to check against a
 * struct tm time.
 *
 *    Syntax = tod ["," tod]
 *       tod = <days><timerange>
 *      days = "Any" | "Wk" | <dayname>+ | nothing
 *   dayname = "Sun" | "Mon" | "Tue" | "Wed" | "Thu" | "Fri" | "Sat"
 * timerange = <start> "-" <end> | nothing
 *     start = <24hrtime>
 *       end = <24hrtime>
 *  24hrtime = {0-9}{0-9}{0-9}{0-9}
 *
 * where start & end are 24-hour times, day names can be either
 * 2- or 3-characters, and a null day or time specification means
 * any time or day.  For example,
 *
 *    Any		any time of any day
 *    MoWeFri0800-0900	Monday+Wednesday+Friday between 8am and 9am
 *    2330-0130		any day, between 11:30pm and 1:30am the next day
 *    TueThu		any time on Tuesday and Thursday
 *
 * White space and other syntactic sugar may be freely inserted between
 * tokens:
 *
 *    MoWeFri/0800-0900	Monday+Wednesday+Friday between 8am and 9am
 *    Tue+Thu		any time on Tuesday and Thursday
 *
 * but may not be inserted between 24-hour times in the time range.
 */
void
TimeOfDay::parse(const char* cp)
{
    reset();
    while (*cp != '\0') {
	while (isspace(*cp))
	    cp++;
	int days = 0;
	if (strneq(cp, "Any", 3)) {		// any day
	    days = 0x7f;
	    cp += 3;
	} else if (strneq(cp, "Wk", 2)) {	// any week day
	    days = 0x3e;
	    cp += 2;
	} else if (isalpha(*cp)) {		// list of days
	    do {
		static const char* dayNames = "Sun Mon Tue Wed Thu Fri Sat ";
		u_int i;
		for (i = 0; dayNames[i] != '\0'; i += 4)
		    if (cp[0] == dayNames[i] && cp[1] == dayNames[i+1])
			break;
		if (dayNames[i] == '\0') {
		    // XXX unknown day
		    break;
		}
		days |= BIT(i>>2);
		cp += (cp[2] == dayNames[i+2] ? 3 : 2);
		for (; !isalnum(*cp) && *cp != ',' && *cp; cp++)
		    ;
	    } while (isalpha(*cp));
	}
	if (days == 0)
	    days = 0x7f;			// default to any day
	// skip to any time range
	while (*cp && *cp != ',' && !isdigit(*cp))
	    cp++;
	time_t start, end;
	if (sscanf(cp, "%u-%u", &start, &end) == 2) {
	    // convert from military time to seconds
	    start = (start/100)*60 + (start%100);
	    end = (end/100)*60 + (end%100);
	} else {
	    // no time spec or invalid spec, use all day
	    start = 0;
	    end = 24*60;
	}
	add(days, start, end);
	// skip to next time-of-day specification
	while (*cp && *cp++ != ',')
	    ;
    }
}

/*
 * Return the next time of day that's OK.
 */
time_t
TimeOfDay::nextTimeOfDay(time_t t) const
{
    struct tm* tm = Sys::localtime(t);
    time_t best = 7*24*60+1;			// 1 week+1 minute 
    time_t hm = tm->tm_hour*60 + tm->tm_min;
    for (const _tod* td = &tod; td; td = td->next) {
	time_t diff = td->nextTime(tm->tm_wday, hm);
	if (diff < best)
	    best = diff;
    }
    return (t + 60*best);
}

_tod::_tod(int d, time_t s, time_t e)
{
    days = d;
    start = s;
    end = e;
    next = NULL;
}
_tod::_tod(const _tod& other)
{
    days = other.days;
    start = other.start;
    end = other.end;
    next = NULL;
}

/*
 * Starting at wday+d, look for an OK day.
 */
int
_tod::nextDay(int d, int wday) const
{
    for (int w = (wday+d)%7; (days & BIT(w)) == 0; w = (w+1)%7)
	d++;
    return d;
}

/*
 * Calculate the time differential to the next
 * time at which <wday,hm> is acceptable.  Note
 * that 0 means it's OK right now.
 */
time_t
_tod::nextTime(int wday, time_t hm) const
{
    int d = (days & BIT(wday)) ? 0 : nextDay(1, wday);
    time_t mins;
    if (start <= end) {
	if (start <= hm) {
	    if (end < hm) {		// outside time range
		mins = (24*60-hm)+start;// time to next start of range
		if (d == 0)		// find next available day
		    d = nextDay(d+1, wday);
		d--;			// time includes day forward
	    } else {			// within time range
		if (d != 0) {		// backup to start of range
		    mins = (24*60-hm)+start;
		    d--;		// time includes day forward
		} else			// ok to go now
		    mins = 0;
	    }
	} else {			// before start of range
	    mins = start - hm;		// push to start of range
	}
    } else {
	if (start <= hm) {		// within time range
	    if (d != 0) {		// backup to start of range
		mins = (24*60-hm)+start;
		d--;			// time includes day forward
	    } else			// ok to go now
		mins = 0;
	} else {
	    if (end < hm)		// outside time range, push to start
		mins = start - hm;
	    else			// ok to go now
		mins = 0;
	}
    }
    return (d*24*60 + mins);
}
