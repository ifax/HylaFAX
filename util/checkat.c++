#include "Str.h"
#include <time.h>

extern int parseAtSyntax(const char*, const struct tm&, struct tm&, fxStr& emsg);

static const char* days[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};
static const char* months[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

static void
print(const char* tag, const struct tm& t)
{
    printf("%-24s: %s %s %u, %u %u:%02u:%02u (%u yday)\n",
	tag,
	days[t.tm_wday],
	months[t.tm_mon], t.tm_mday, 1900+t.tm_year,
	t.tm_hour, t.tm_min, t.tm_sec,
	t.tm_yday
    );
}

static void
doit(const char* s)
{
    time_t t = time(0);
    struct tm now = *localtime(&t);
    struct tm at;
    fxStr emsg;
    if (parseAtSyntax(s, now, at, emsg))
	print(s, at);
    else
	printf("%-24s: %s.\n", s, (const char*) emsg);
}

int
main(int argc, char* argv[])
{
    if (argc == 1) {
	doit("8");
	doit("10");
	doit("80");
	doit("1100");
	doit("2300zulu");
	doit("8pm");
	doit("8am");
	doit("10am");
	doit("10pm");
	doit("0815am");
	doit("8:15am");
	doit("now");
	doit("noon");
	doit("midnight");
	doit("next");
	doit("0815am Jan 24");
	doit("8:15am Jan 24");
	doit("1:30 today");
	doit("1:30 tomorrow");
	doit("midnight Monday");
	doit("midnight mon");
	doit("next Tuesday");
	doit("next tue");
	doit("noon Wednesday");
	doit("noon wed");
	doit("next Thursday");
	doit("next thu");
	doit("8pm Friday");
	doit("8pm fri");
	doit("5 pm Friday");
	doit("6am Saturday");
	doit("6am sat");
	doit("7:30 Sunday");
	doit("7:30 sun");
	doit("8:15am Jan 24, 1993");
	doit("8:15am Jan 24, 1994");
	doit("now + 1 minute");
	doit("now + 1 hour");
	doit("now + 1 day");
	doit("now + 1 week");
	doit("now + 1 month");
	doit("now + 1 year");
	doit("now + 120 minutes");
    } else {
	fxStr s;
	for (int i = 1; i < argc; i++)
	    s = s | " " | fxStr(argv[i]);
	doit(s);
    }
    return (0);
}
