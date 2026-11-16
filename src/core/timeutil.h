/*
 * AmiRocket - timestamps. Everything is kept as milliseconds since the
 * Unix epoch (UTC) in a 64-bit integer; no libc time functions are used,
 * because libnix has no time zone support.
 */
#ifndef AR_TIMEUTIL_H
#define AR_TIMEUTIL_H

typedef long long ar_ms;

ar_ms iso8601_to_ms(const char *s);           /* 0 on parse error */
void  ms_to_iso8601(ar_ms ms, char out[32]);  /* 2026-11-16T07:21:13.123Z */
ar_ms http_date_to_ms(const char *s);         /* RFC 1123, 0 on error */

/* Formatting of a local time (UTC ms + offset). */
void  ms_format_hm(ar_ms local_ms, char out[8]);        /* 07:21 */
void  ms_format_date(ar_ms local_ms, char out[16]);     /* 2026-11-16 */
long  ms_day_number(ar_ms local_ms);                    /* days since epoch */

long  days_from_civil(long y, unsigned m, unsigned d);
void  civil_from_days(long z, long *y, unsigned *m, unsigned *d);

#endif
