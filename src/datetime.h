#ifndef GNUMERIC_DATETIME_H
#define GNUMERIC_DATETIME_H

#include "gnumeric.h"
#include "numbers.h"
#include <time.h>

/*
 * Naming conventions:
 *
 * "g": a GDate *.
 * "timet": Unix' time_t.
 * "serial": Excel serial day number.
 * "serial_raw": serial plus time as fractional day.
 */

/* These do not round and produces fractional values, i.e., includes time.  */
gnum_float datetime_value_to_serial_raw (const Value *v);
gnum_float datetime_timet_to_serial_raw (time_t t);

/* These are date-only, no time.  */
int datetime_value_to_serial (const Value *v);
int datetime_timet_to_serial (time_t t);
GDate* datetime_value_to_g (const Value *v);
int datetime_g_to_serial (GDate *date);
GDate* datetime_serial_to_g (int serial);
int datetime_serial_raw_to_serial (gnum_float raw);

/* These are time-only assuming a 24h day.  It probably loses completely on */
/* days with summer time ("daylight savings") changes.  */
int datetime_value_to_seconds (const Value *v);
int datetime_timet_to_seconds (time_t t);
int datetime_serial_raw_to_seconds (gnum_float raw);

int datetime_g_days_between (GDate *date1, GDate *date2);

/* Number of full months between date1 and date2. */
/* largest value s.t. g_date_add_months (date1, result) <= date2 */
/* except that if the day is decreased in g_date_add_monts, treat
   that as > the date it is decreased to. */
/* ( datetime_g_months_between ( March 31, April 30 ) == 0
     even though g_date_add_months ( Mar 31, 1 ) <= Apr 30.... */
int datetime_g_months_between (GDate *date1, GDate *date2);
/* Number of full years between date1 and date2. */
/* (g_date_add_years (date1, result) <= date2; largest such value. */
/*  treat add_years (29-feb, x) > 28-feb ) */
int datetime_g_years_between (GDate *date1, GDate *date2);

#endif
