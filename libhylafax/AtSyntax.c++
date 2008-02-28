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
#include "Str.h"

#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "NLS.h"

#define	TM_YEAR_BASE	1900		// tm times all start here...
#define	EPOCH_YEAR	1970
#define	EPOCH_WDAY	4		// January 1, 1970 was a Thursday
#define	DAYSPERWEEK	7

#define HOUR		60		// minutes per hour
#define HALFDAY		(12 * HOUR)	// half a day (12 hours)
#define FULLDAY		(24 * HOUR)	// a full day (24 hours)

static int
isLeapYear(const tm& at) {
    int y = at.tm_year + TM_YEAR_BASE;
    return ((y % 4) == 0 && (y % 100) != 0 || (y % 400) == 0);
}

#ifdef streq
#undef streq
#endif
#define	streq(a,b,n)	(strncasecmp(a,b,n) == 0)

static	bool checkDay(const char*& cp, int& day);
static	void adjustDay(struct tm& at, int day, const struct tm&);
static	bool checkMonth(const char*& cp, int& month);
static	bool parseMonthAndYear(const char*&, const struct tm& ref,
	    struct tm& at, fxStr& emsg);
static	bool parseMultiplier(const char* cp, struct tm& at, fxStr& emsg);
static	const char* whitespace(const char* cp);
static	void fixup(struct tm& at);

static	void _atSyntax(fxStr& emsg, const char* fmt, ...);
static	void _atError(fxStr& emsg, const char* fmt, ...);

static int
operator<(const struct tm& a, const struct tm& b)
{
    return (
	(a.tm_year < b.tm_year)
     || (a.tm_year == b.tm_year && a.tm_yday < b.tm_yday)
     || (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday &&
	 a.tm_hour < b.tm_hour)
     || (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday &&
	 a.tm_hour == b.tm_hour && a.tm_min < b.tm_min)
    );
}

/*
 * Parse at-style time [date] [+increment] syntax
 * and return the resultant time, relative to the
 * specified reference time.
 */
int
parseAtSyntax(const char* s, const struct tm& ref, struct tm& at0, fxStr& emsg)
{
    struct tm at = ref;

    /*
     * Time specification.
     */
    const char* cp = s = whitespace(s);
    int v = 0;
    if (isdigit(cp[0])) {
	do
	    v = v*10 + (*cp - '0');
	while (isdigit(*++cp));
	if (cp-s < 3)			// only hours specified
	    v *= HOUR;
	else
	    v = (v/100)*HOUR + v%100;
	if (cp[0] == ':') {		// HH::MM notation
	    if (isdigit(cp[1]) && isdigit(cp[2])) {
		int min = 10*(cp[1]-'0') + (cp[2]-'0');
		if (min >= 60) {
		    _atError(emsg, NLS::TEXT("Illegal minutes value %u"), min);
		    return (false);
		}
		v += min;
		cp += 3;
	    } else {
		_atSyntax(emsg, NLS::TEXT("expecting HH:MM"));
		return (false);
	    }
	}
	cp = whitespace(cp);
	if (streq(cp, "am", 2)) {
	    if (v >= HALFDAY+HOUR) {
		_atError(emsg, NLS::TEXT("%u:%02u is not an AM value"), v/HOUR, v%HOUR);
		return (false);
	    }
	    if (HALFDAY <= v && v < HALFDAY+HOUR)
		v -= HALFDAY;
	    cp += 2;
	} else if (streq(cp, "pm", 2)) {
	    if (v >= HALFDAY+HOUR) {
		_atError(emsg, NLS::TEXT("%u:%02u is not a PM value"), v/HOUR, v%HOUR);
		return (false);
	    }
	    if (v < HALFDAY)
		v += HALFDAY;
	    cp += 2;
	}
    } else {
	if (streq(cp, "noon", 4)) {
	    v = HALFDAY;
	    cp += 4;
	} else if (streq(cp, "midnight", 8)) {
	    v = 0;
	    cp += 8;
	} else if (streq(cp, "now", 3)) {
	    v = at.tm_hour*HOUR + at.tm_min;
	    cp += 3;
	} else if (streq(cp, "next", 4)) {
	    v = at.tm_hour*HOUR + at.tm_min;
	    cp += 4;
	} else {
	    _atSyntax(emsg, NLS::TEXT("unrecognized symbolic time \"%s\""), cp);
	    return (false);
	}
    }
    if ((unsigned) v >= FULLDAY) {
	_atError(emsg, NLS::TEXT("Illegal time value; out of range"));
	return (false);
    }
    at.tm_hour = v/HOUR;
    at.tm_min = v%HOUR;
    at.tm_sec = 0;			// NB: no way to specify seconds

    /*
     * Check for optional date.
     */
    cp = whitespace(cp);
    if (checkMonth(cp, v)) {
	at.tm_mon = v;
	if (!parseMonthAndYear(cp, ref, at, emsg))
	    return (false);
    } else if (checkDay(cp, v)) {
	adjustDay(at, v, ref);
    } else {
	if (streq(cp, "today", 5)) {
	    cp += 5;
	} else if (streq(cp, "tomorrow", 8)) {
	    at.tm_yday++;
	    cp += 8;
	} else if (cp[0] != '\0' && cp[0] != '+') {
	    _atSyntax(emsg, NLS::TEXT("expecting \"+\" after time"));
	    return (false);
	}
	/*
	 * Adjust the date according to whether it is before ``now''.
	 */
	if (at < ref)
	    at.tm_yday++;
    }
    /*
     * Process any increment expression.
     */
    if (cp[0] == '+' && !parseMultiplier(++cp, at, emsg))
	return (false);
    fixup(at);
    if (at < ref) {
	_atError(emsg, NLS::TEXT("Invalid date/time; time must be in the future"));
	return (false);
    }
    at0 = at;
    return (true);
}

/*
 * Return a pointer to the next non-ws
 * item in the character string.
 */
static const char*
whitespace(const char* cp)
{
    while (isspace(*cp))
	cp++;
    return (cp);
}

#define	N(a)	(sizeof (a) / sizeof (a[0]))

/*
 * Check for a day argument and, if found,
 * return the day number [0..6] and update
 * the character pointer.
 */
static bool
checkDay(const char*& cp, int& day)
{
    static const char* days[] = {
	"sunday", "monday", "tuesday", "wednesday",
	"thursday", "friday", "saturday"
    };
    for (u_int i = 0; i < N(days); i++) {
	u_int len = strlen(days[i]);
again:
	if (streq(cp, days[i], len)) {
	    cp += len;
	    day = i;
	    return (true);
	}
	if (len != 3) {			// an Algol-style for loop...
	    len = 3;
	    goto again;
	}
    }
    return (false);
}

/*
 * Adjust tm_yday according to whether or not the
 * specified day [0...6] is before or after the
 * current week day.  Note that if they are the
 * same day, then the specified day is assumed to
 * in the next week.  This routine assumes that
 * tm_wday is correct; which is only true when
 * setup from ``now''.
 */
static void
adjustDay(struct tm& at, int day, const struct tm& ref)
{
    if (at.tm_wday < day)
	at.tm_yday += day - at.tm_wday;
    else if (at.tm_wday > day)
	at.tm_yday += (DAYSPERWEEK - at.tm_wday) + day;
    else if (at < ref)
	at.tm_yday += DAYSPERWEEK;
}

static const char* months[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

/*
 * Check for a month parameter and if found,
 * return the month index [0..11] and update
 * the character pointer.
 */
static bool
checkMonth(const char*& cp, int& month)
{
    for (u_int i = 0; i < N(months); i++) {
	u_int len = strlen(months[i]);
again:
	if (streq(cp, months[i], len)) {
	    cp += len;
	    month = i;
	    return (true);
	}
	if (len != 3) {			// an Algol-style for loop...
	    len = 3;
	    goto again;
	}
    }
    return (false);
}

/*
 * The number of days in each month of the year.
 */
static const int nonLeapYear[12] =
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static const int leapYear[12] =
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static const int* daysInMonth[2] = { nonLeapYear, leapYear };

static void
adjustYDay(struct tm& t)
{
    // adjust year day according to month
    const int* days = daysInMonth[isLeapYear(t)];
    t.tm_yday = t.tm_mday;
    for (int i = 0; i < t.tm_mon; i++)
	t.tm_yday += days[i];
}

/*
 * Parse an expected day [, year] expression.
 */
static bool
parseMonthAndYear(const char*& cp, const struct tm& ref, struct tm& at, fxStr& emsg)
{
    char* tp;
    at.tm_mday = (u_int) strtoul(cp, &tp, 10);
    if (tp == cp) {
	_atSyntax(emsg, NLS::TEXT("missing or invalid day of month"));
	return (false);
    }
    at.tm_mday--;			// tm_mday is [0..31]
    tp = (char*) whitespace(tp);
    if (*tp == ',') {			// ,[ ]*YYYY
	char* xp;
	u_int year = (u_int) strtoul(++tp, &xp, 10);
	if (tp == xp) {
	    _atSyntax(emsg, NLS::TEXT("missing year"));
	    return (false);
	}
	if (year < TM_YEAR_BASE) {
	    _atError(emsg, NLS::TEXT("Sorry, cannot handle dates before %u"),
		TM_YEAR_BASE);
	    return (false);
	}
	at.tm_year = year - TM_YEAR_BASE;
	adjustYDay(at);
	tp = xp;
    } else {				// implicit year
	/*
	 * If the specified date is before ``now'', then it
	 * must be for next year.  Note that we have to do
	 * this in two steps since yday can vary because of
	 * leap years.
	 */
	adjustYDay(at);
	if (at.tm_yday < ref.tm_yday ||
	  (at.tm_yday == ref.tm_yday && at.tm_hour < ref.tm_hour)) {
	    at.tm_year++;
	    adjustYDay(at);
	}
    }
    const int* days = daysInMonth[isLeapYear(at)];
    if (at.tm_mday > days[at.tm_mon]) {
	_atError(emsg, NLS::TEXT("Invalid day of month, %s has only %u days"),
	    months[at.tm_mon], days[at.tm_mon]);
	return (false);
    }
    cp = tp;
    return (true);
}

/*
 * Parse an expected multiplier expression.
 */
static bool
parseMultiplier(const char* cp, struct tm& at, fxStr& emsg)
{
    cp = whitespace(cp);
    if (!isdigit(cp[0])) {
	_atSyntax(emsg, NLS::TEXT("expecting number after \"+\""));
	return (false);
    }
    int v = 0;
    for (; isdigit(*cp); cp++)
	v = v*10 + (*cp - '0');
    cp = whitespace(cp);
    if (*cp == '\0') {
	_atSyntax(emsg, NLS::TEXT("\"+%u\" without unit"), v);
	return (false);
    }
    if (streq(cp, "minute", 6))
	at.tm_min += v;
    else if (streq(cp, "hour", 4))
	at.tm_hour += v;
    else if (streq(cp, "day", 3))
	at.tm_yday += v;
    else if (streq(cp, "week", 4))
	at.tm_yday += DAYSPERWEEK*v;
    else if (streq(cp, "month", 5)) {
	at.tm_mon += v;
	while (at.tm_mon >= 12)
	    at.tm_mon -= 12, at.tm_year++;
	adjustYDay(at);
    } else if (streq(cp, "year", 4))
	at.tm_year += v;
    else {
	_atError(emsg, NLS::TEXT("Unknown increment unit \"%s\""), cp);
	return (false);
    }
    return (true);
}

/*
 * Correct things in case we've slopped over as a result
 * of an increment expression or similar calculation.
 * Note that this routine ``believes'' the minutes, hours,
 * year, and yday values and recalculates month, mday,
 * and wday values based on the believed values.
 */
static void
fixup(struct tm& at)
{
    while (at.tm_min >= HOUR)
	at.tm_hour++, at.tm_min -= HOUR;
    while (at.tm_hour >= FULLDAY)
	at.tm_yday++, at.tm_hour -= FULLDAY;
    bool leap;
    int daysinyear;
    for (;;) {
	leap = isLeapYear(at);
	daysinyear = leap ? 366 : 365;
	if (at.tm_yday < daysinyear)
	    break;
	at.tm_yday -= daysinyear, at.tm_year++;
    }
    /*
     * Now recalculate derivative values
     * to insure everything is consistent.
     */
    const int* days = daysInMonth[leap];
    at.tm_mday = at.tm_yday;
    for (at.tm_mon = 0; at.tm_mday >= days[at.tm_mon]; at.tm_mon++)
	at.tm_mday -= days[at.tm_mon];
    at.tm_mday++;			// NB: [1..31]

    int eday = at.tm_yday;
    tm temp;
    temp.tm_year = EPOCH_YEAR - TM_YEAR_BASE;
    while (temp.tm_year < at.tm_year) {
        eday += isLeapYear(temp) ? 366 : 365;
        temp.tm_year++;
    }
    at.tm_wday = (EPOCH_WDAY + eday) % DAYSPERWEEK;
}

static void
_atSyntax(fxStr& emsg, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emsg = NLS::TEXT("Syntax error, ") | fxStr::vformat(fmt, ap);
    va_end(ap);
}

static void
_atError(fxStr& emsg, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emsg = fxStr::vformat(fmt, ap);
    va_end(ap);
}
