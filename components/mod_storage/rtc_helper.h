// MIT License
// Copyright (c) 2025 UniTheCat
#include <string.h>

#define TIME_OFFSET 5*60*60

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_DAY 86400
#define IS_LEAP_YEAR(year) ((((year) % 4 == 0) && ((year) % 100 != 0)) || ((year) % 400 == 0))

// Array of days in each month (non-leap year)
const int DAYS_IN_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const char RTC_DIGITS[] = "0123456789";

typedef struct {
	int year;
	int month;
	int day;
} rtc_date_t;

typedef struct {
	int hr;
	int min;
	int sec;
	int ms;
} rtc_time_t;

typedef struct {
	rtc_date_t date;
	rtc_time_t time;
} rtc_datetime_t;


// * brief calculate days of the current year. eg. 2020-02-05 is 36 day

int RTC_days_of_year(int year, int month, int day) {
	int day_of_year = 0;

	// Add days from January to month-1
	for (int m = 0; m < month - 1; m++) {
		day_of_year += DAYS_IN_MONTH[m];
	}

	// Add extra day for February (full month) if it's a leap year
	if (month > 2 && IS_LEAP_YEAR(year)) {
		day_of_year += 1;
	}

	// Add days in the current month
	day_of_year += day;

	return day_of_year;
}


// * brief Get seconds count from date

int RTC_get_seconds(
	int year, int month, int day, int hr, int min, int sec
) {
	//# Validate input
	if (month < 1 || month > 12 || day < 1 || day > 31 ||
		hr > 23 || min > 59 || sec > 59 || year < 1970) { return 0; }

	// calculate days of the current year, -1 for 0-based days
	int days = RTC_days_of_year(year, month, day) - 1;

	// add the days count excluding the current year
	// (start from epoch time 1970-01-01 00:00:00)
	for (int y=1970; y < year; y++) {
		days += IS_LEAP_YEAR(y) ? 366 : 365;
	}

	// calculate total seconds
	return days * SECONDS_PER_DAY +
			hr * SECONDS_PER_HOUR +
			min * SECONDS_PER_MINUTE + sec;
}


// * brief Get date from total seconds
// * @param total_seconds: Total seconds
// * @param year_base: Base year to start from use 1970 for epoch
// * @return Date

rtc_date_t RTC_get_date(int total_seconds, int year_base, int timeOffset) {
	total_seconds -= timeOffset;

	rtc_date_t output = {
		.year = year_base,
		.month = 1,
		.day = 1
	};

	// Days since epoch
	int days_remaining = total_seconds / SECONDS_PER_DAY;

	// Find the year
	while(1) {
		int days_in_year = IS_LEAP_YEAR(output.year) ? 366 : 365;
		if (days_remaining < days_in_year) break;
		days_remaining -= days_in_year;
		output.year++;
	}

	// find the month
	for (int m = 0; m < 12; m++) {
		int days_in_month = DAYS_IN_MONTH[m];
		if (m == 1 && IS_LEAP_YEAR(output.year)) days_in_month = 29;
		if (days_remaining < days_in_month) break;

		days_remaining -= days_in_month;
		output.month++;
	}

	// add 1 because days_remaining is 0-based
	output.day = days_remaining + 1;

	return output;
}


// * brief Get time from total seconds
// * @param total_seconds: Total seconds
// * @param ms: additional milliseconds
// * @return Time

rtc_time_t RTC_get_time(int total_seconds, int ms, int timeOffset) {
	total_seconds -= timeOffset;
	int minutes = total_seconds / 60;

	return (rtc_time_t) {
		.hr = (minutes / 60) % 24,
		.min = minutes % 60,
		.sec = total_seconds % 60,
		.ms = ms
	};
}


// * brief Get date string
// * @param date: Date
// * @param str: String buffer
// * @param separator: Separator character
// * @param full_year: 1 = 4-digit year, 0 = 2-digit year
// @ note: minimum 11 characters for the string buffer

int RTC_get_dateStr(char *str, rtc_date_t date, char separator, int full_year) {
	int pos = 0;

    if (full_year) {
        // Full year (4 digits)
        str[pos++] = RTC_DIGITS[date.year / 1000];
        str[pos++] = RTC_DIGITS[(date.year % 1000) / 100];
        str[pos++] = RTC_DIGITS[(date.year % 100) / 10];
        str[pos++] = RTC_DIGITS[date.year % 10];
    } else {
        // 2-digit year
        int year2 = date.year % 100;
        str[pos++] = RTC_DIGITS[year2 / 10];
        str[pos++] = RTC_DIGITS[year2 % 10];
    }
	if (separator) str[pos++] = separator;

	// Month
	str[pos++] = RTC_DIGITS[date.month / 10];
	str[pos++] = RTC_DIGITS[date.month % 10];
	if (separator) str[pos++] = separator;

	// Day
	str[pos++] = RTC_DIGITS[date.day / 10];
	str[pos++] = RTC_DIGITS[date.day % 10];
	str[pos] = '\0';

	return full_year ? 10 : 8;		// YYYY-MM-DD = 10, YYYYMMDD = 8
}

int RTC_dateStr_fromEpoch(
	char *str, int total_seconds, char separator, int full_year, int timeOffset
) {
	rtc_date_t date = RTC_get_date(total_seconds, 1970, timeOffset);
	return RTC_get_dateStr(str, date, separator, full_year);
}

// * brief Get time string
// * @param time: Time
// * @param str: String buffer
// * @param separator: Separator character
// @ note: minimum 9 characters for the string buffer

int RTC_get_timeStr(char *str, rtc_time_t time, char separator) {
	// Write string
	str[0] = RTC_DIGITS[time.hr / 10];
	str[1] = RTC_DIGITS[time.hr % 10];

	if (separator) {
		str[2] = separator;
		str[3] = RTC_DIGITS[time.min / 10];
		str[4] = RTC_DIGITS[time.min % 10];
		str[5] = separator;
		str[6] = RTC_DIGITS[time.sec / 10];
		str[7] = RTC_DIGITS[time.sec % 10];
		str[8] = '\0';
	} else {
		str[2] = RTC_DIGITS[time.min / 10];
		str[3] = RTC_DIGITS[time.min % 10];
		str[4] = RTC_DIGITS[time.sec / 10];
		str[5] = RTC_DIGITS[time.sec % 10];
		str[6] = '\0';
	}

	return separator ? 8 : 6;  // HH:MM:SS = 8, HHMMSS = 6
}

int RTC_timeStr_fromEpoch(char *str, int total_seconds, char separator, int timeOffset) {
	rtc_time_t time = RTC_get_time(total_seconds, 0, timeOffset);
	return RTC_get_timeStr(str, time, separator);
}


// @ note: minimum 20 characters for safety

int RTC_datetimeStr_fromEpoch(char *str, int total_seconds, int timeOffset) {
	int date_len = RTC_dateStr_fromEpoch(str, total_seconds, '/', 0, timeOffset);
	str[date_len] = ' ';
	int time_len = RTC_timeStr_fromEpoch(str + date_len + 1, total_seconds, ':', timeOffset);
	return date_len + time_len + 1;
}