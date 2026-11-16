#include "timeutil.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

/* Howard Hinnant's civil calendar algorithms. */
long days_from_civil(long y, unsigned m, unsigned d)
{
    long era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

void civil_from_days(long z, long *y, unsigned *m, unsigned *d)
{
    long era;
    unsigned doe, yoe, doy, mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y += *m <= 2;
}

static int digits(const char *s, int n, long *out)
{
    long v = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

ar_ms iso8601_to_ms(const char *s)
{
    long y, mo, d, h = 0, mi = 0, se = 0, frac = 0;
    ar_ms ms;
    int fdig = 0;

    if (!s || strlen(s) < 10)
        return 0;
    if (digits(s, 4, &y) || s[4] != '-' || digits(s + 5, 2, &mo) || s[7] != '-' || digits(s + 8, 2, &d))
        return 0;
    s += 10;
    if (*s == 'T' || *s == ' ') {
        s++;
        if (digits(s, 2, &h) || s[2] != ':' || digits(s + 3, 2, &mi))
            return 0;
        s += 5;
        if (*s == ':') {
            if (digits(s + 1, 2, &se))
                return 0;
            s += 3;
        }
        if (*s == '.') {
            s++;
            while (*s >= '0' && *s <= '9') {
                if (fdig < 3) {
                    frac = frac * 10 + (*s - '0');
                    fdig++;
                }
                s++;
            }
            while (fdig < 3) {
                frac *= 10;
                fdig++;
            }
        }
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60)
        return 0;
    ms = (ar_ms)days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400000LL +
         (ar_ms)h * 3600000LL + (ar_ms)mi * 60000LL + (ar_ms)se * 1000LL + frac;
    if (*s == '+' || *s == '-') {
        long oh, om = 0;
        int sign = *s == '-' ? -1 : 1;
        if (digits(s + 1, 2, &oh))
            return 0;
        if (s[3] == ':')
            digits(s + 4, 2, &om);
        else
            digits(s + 3, 2, &om);
        ms -= sign * ((ar_ms)oh * 3600000LL + (ar_ms)om * 60000LL);
    }
    return ms;
}

void ms_to_iso8601(ar_ms ms, char out[32])
{
    long days = (long)(ms >= 0 ? ms / 86400000LL : (ms - 86399999LL) / 86400000LL);
    long rem = (long)(ms - (ar_ms)days * 86400000LL);
    long y;
    unsigned m, d;

    civil_from_days(days, &y, &m, &d);
    snprintf(out, 32, "%04ld-%02u-%02uT%02ld:%02ld:%02ld.%03ldZ", y, m, d,
             rem / 3600000L, (rem / 60000L) % 60, (rem / 1000L) % 60, rem % 1000L);
}

ar_ms http_date_to_ms(const char *s)
{
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    long d, y, h, mi, se;
    int m;
    const char *p;

    if (!s)
        return 0;
    p = strchr(s, ',');
    p = p ? p + 1 : s;
    while (*p == ' ')
        p++;
    /* "16 Nov 2026 07:48:10 GMT" */
    if (digits(p, 2, &d) || p[2] != ' ')
        return 0;
    p += 3;
    for (m = 0; m < 12; m++)
        if (ar_strncasecmp(p, mon[m], 3) == 0)
            break;
    if (m == 12 || p[3] != ' ')
        return 0;
    p += 4;
    if (digits(p, 4, &y) || p[4] != ' ')
        return 0;
    p += 5;
    if (digits(p, 2, &h) || p[2] != ':' || digits(p + 3, 2, &mi) || p[5] != ':' || digits(p + 6, 2, &se))
        return 0;
    return (ar_ms)days_from_civil(y, (unsigned)m + 1, (unsigned)d) * 86400000LL +
           (ar_ms)h * 3600000LL + (ar_ms)mi * 60000LL + (ar_ms)se * 1000LL;
}

long ms_day_number(ar_ms ms)
{
    return (long)(ms >= 0 ? ms / 86400000LL : (ms - 86399999LL) / 86400000LL);
}

void ms_format_hm(ar_ms ms, char out[8])
{
    long rem = (long)(ms - (ar_ms)ms_day_number(ms) * 86400000LL);
    snprintf(out, 8, "%02ld:%02ld", rem / 3600000L, (rem / 60000L) % 60);
}

void ms_format_date(ar_ms ms, char out[16])
{
    long y;
    unsigned m, d;

    civil_from_days(ms_day_number(ms), &y, &m, &d);
    snprintf(out, 16, "%04ld-%02u-%02u", y, m, d);
}
