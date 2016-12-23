/*
 * Copyright 2013 MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "bson-compat.h"
#include "bson-macros.h"
#include "bson-error.h"
#include "bson-iso8601-private.h"
#include "bson-json.h"

#ifndef _WIN32
#include "bson-timegm-private.h"
#endif


static bool
get_tok (const char *terminals,
         const char **ptr,
         int32_t *remaining,
         const char **out,
         int32_t *out_len)
{
   const char *terminal;
   bool found_terminal = false;

   if (!*remaining) {
      *out = "";
      *out_len = 0;
   }

   *out = *ptr;
   *out_len = -1;

   for (; *remaining && !found_terminal;
        (*ptr)++, (*remaining)--, (*out_len)++) {
      for (terminal = terminals; *terminal; terminal++) {
         if (**ptr == *terminal) {
            found_terminal = true;
            break;
         }
      }
   }

   if (!found_terminal) {
      (*out_len)++;
   }

   return found_terminal;
}

static bool
digits_only (const char *str, int32_t len)
{
   int i;

   for (i = 0; i < len; i++) {
      if (!isdigit (str[i])) {
         return false;
      }
   }

   return true;
}

static bool
parse_num (const char *str,
           int32_t len,
           int32_t digits,
           int32_t min,
           int32_t max,
           int32_t *out)
{
   int i;
   int magnitude = 1;
   int32_t value = 0;

   if ((digits >= 0 && len != digits) || !digits_only (str, len)) {
      return false;
   }

   for (i = 1; i <= len; i++, magnitude *= 10) {
      value += (str[len - i] - '0') * magnitude;
   }

   if (value < min || value > max) {
      return false;
   }

   *out = value;

   return true;
}

bool
_bson_iso8601_date_parse (const char *str,
                          int32_t len,
                          int64_t *out,
                          bson_error_t *error)
{
   const char *ptr;
   int32_t remaining = len;

   const char *year_ptr;
   const char *month_ptr;
   const char *day_ptr;
   const char *hour_ptr;
   const char *min_ptr;
   const char *sec_ptr;
   const char *millis_ptr;
   const char *tz_ptr;

   int32_t year_len = 0;
   int32_t month_len = 0;
   int32_t day_len = 0;
   int32_t hour_len = 0;
   int32_t min_len = 0;
   int32_t sec_len = 0;
   int32_t millis_len = 0;
   int32_t tz_len = 0;

   int32_t year;
   int32_t month;
   int32_t day;
   int32_t hour;
   int32_t min;
   int32_t sec = 0;
   int64_t millis = 0;
   int32_t tz_adjustment = 0;

#ifdef BSON_OS_WIN32
   SYSTEMTIME win_sys_time;
   FILETIME win_file_time;
   int64_t win_time_offset;
   int64_t win_epoch_difference;
#else
   struct bson_tm posix_date = {0};
#endif

#define DATE_PARSE_ERR(msg)                                \
   bson_set_error (error,                                  \
                   BSON_ERROR_JSON,                        \
                   BSON_JSON_ERROR_READ_INVALID_PARAM,     \
                   "Could not parse \"%s\" as date: " msg, \
                   str);                                   \
   return false

#define DEFAULT_DATE_PARSE_ERR                                                 \
   DATE_PARSE_ERR ("use ISO8601 format yyyy-mm-ddThh:mm plus timezone, either" \
                   " \"Z\" or like \"+0500\"")

   ptr = str;

   /* we have to match at least yyyy-mm-ddThh:mm */
   if (!(get_tok ("-", &ptr, &remaining, &year_ptr, &year_len) &&
         get_tok ("-", &ptr, &remaining, &month_ptr, &month_len) &&
         get_tok ("T", &ptr, &remaining, &day_ptr, &day_len) &&
         get_tok (":", &ptr, &remaining, &hour_ptr, &hour_len) &&
         get_tok (":+-Z", &ptr, &remaining, &min_ptr, &min_len))) {
      DEFAULT_DATE_PARSE_ERR;
   }

   /* if the minute has a ':' at the end look for seconds */
   if (min_ptr[min_len] == ':') {
      if (remaining < 2) {
         DATE_PARSE_ERR ("reached end of date while looking for seconds");
      }

      get_tok (".+-Z", &ptr, &remaining, &sec_ptr, &sec_len);

      if (!sec_len) {
         DATE_PARSE_ERR ("minute ends in \":\" seconds is required");
      }
   }

   /* if we had a second and it is followed by a '.' look for milliseconds */
   if (sec_len && sec_ptr[sec_len] == '.') {
      if (remaining < 2) {
         DATE_PARSE_ERR ("reached end of date while looking for milliseconds");
      }

      get_tok ("+-Z", &ptr, &remaining, &millis_ptr, &millis_len);

      if (!millis_len) {
         DATE_PARSE_ERR ("seconds ends in \".\", milliseconds is required");
      }
   }

   /* backtrack by 1 to put ptr on the timezone */
   ptr--;
   remaining++;

   get_tok ("", &ptr, &remaining, &tz_ptr, &tz_len);

   if (!parse_num (year_ptr, year_len, 4, -9999, 9999, &year)) {
      DATE_PARSE_ERR ("year must be an integer");
   }

   /* values are as in struct tm */
   year -= 1900;

   if (!parse_num (month_ptr, month_len, 2, 1, 12, &month)) {
      DATE_PARSE_ERR ("month must be an integer");
   }

   /* values are as in struct tm */
   month -= 1;

   if (!parse_num (day_ptr, day_len, 2, 1, 31, &day)) {
      DATE_PARSE_ERR ("day must be an integer");
   }

   if (!parse_num (hour_ptr, hour_len, 2, 0, 23, &hour)) {
      DATE_PARSE_ERR ("hour must be an integer");
   }

   if (!parse_num (min_ptr, min_len, 2, 0, 59, &min)) {
      DATE_PARSE_ERR ("minute must be an integer");
   }

   if (sec_len && !parse_num (sec_ptr, sec_len, 2, 0, 60, &sec)) {
      DATE_PARSE_ERR ("seconds must be an integer");
   }

   if (tz_len > 0) {
      if (tz_ptr[0] == 'Z' && tz_len == 1) {
         /* valid */
      } else if (tz_ptr[0] == '+' || tz_ptr[0] == '-') {
         int32_t tz_hour;
         int32_t tz_min;

         if (tz_len != 5 || !digits_only (tz_ptr + 1, 4)) {
            DATE_PARSE_ERR ("could not parse timezone");
         }

         if (!parse_num (tz_ptr + 1, 2, -1, -23, 23, &tz_hour)) {
            DATE_PARSE_ERR ("timezone hour must be at most 23");
         }

         if (!parse_num (tz_ptr + 3, 2, -1, 0, 59, &tz_min)) {
            DATE_PARSE_ERR ("timezone minute must be at most 59");
         }

         /* we inflect the meaning of a 'positive' timezone.  Those are hours
          * we have to substract, and vice versa */
         tz_adjustment =
            (tz_ptr[0] == '-' ? 1 : -1) * ((tz_min * 60) + (tz_hour * 60 * 60));

         if (!(tz_adjustment > -86400 && tz_adjustment < 86400)) {
            DATE_PARSE_ERR ("timezone offset must be less than 24 hours");
         }
      } else {
         DATE_PARSE_ERR ("timezone is required");
      }
   }

   if (millis_len > 0) {
      int i;
      int magnitude;
      millis = 0;

      if (millis_len > 3 || !digits_only (millis_ptr, millis_len)) {
         DATE_PARSE_ERR ("milliseconds must be an integer");
      }

      for (i = 1, magnitude = 1; i <= millis_len; i++, magnitude *= 10) {
         millis += (millis_ptr[millis_len - i] - '0') * magnitude;
      }

      if (millis_len == 1) {
         millis *= 100;
      } else if (millis_len == 2) {
         millis *= 10;
      }

      if (millis < 0 || millis > 1000) {
         DATE_PARSE_ERR ("milliseconds must be at least 0 and less than 1000");
      }
   }

#ifdef BSON_OS_WIN32
   win_sys_time.wMilliseconds = millis;
   win_sys_time.wSecond = sec;
   win_sys_time.wMinute = min;
   win_sys_time.wHour = hour;
   win_sys_time.wDay = day;
   win_sys_time.wDayOfWeek = -1; /* ignored */
   win_sys_time.wMonth = month + 1;
   win_sys_time.wYear = year + 1900;

   /* the wDayOfWeek member of SYSTEMTIME is ignored by this function */
   if (SystemTimeToFileTime (&win_sys_time, &win_file_time) == 0) {
      return 0;
   }

   /* The Windows FILETIME structure contains two parts of a 64-bit value
    * representing the
    * number of 100-nanosecond intervals since January 1, 1601
    */
   win_time_offset = (((uint64_t) win_file_time.dwHighDateTime) << 32) |
                     win_file_time.dwLowDateTime;

   /* There are 11644473600 seconds between the unix epoch and the windows epoch
    * 100-nanoseconds = milliseconds * 10000
    */
   win_epoch_difference = 11644473600000 * 10000;

   /* removes the diff between 1970 and 1601 */
   win_time_offset -= win_epoch_difference;

   /* 1 milliseconds = 1000000 nanoseconds = 10000 100-nanosecond intervals */
   millis = win_time_offset / 10000;
#else
   posix_date.tm_sec = sec;
   posix_date.tm_min = min;
   posix_date.tm_hour = hour;
   posix_date.tm_mday = day;
   posix_date.tm_mon = month;
   posix_date.tm_year = year;
   posix_date.tm_wday = 0;
   posix_date.tm_yday = 0;

   millis = 1000 * _bson_timegm (&posix_date) + millis;

#endif

   millis += tz_adjustment * 1000;
   *out = millis;

   return true;
}
