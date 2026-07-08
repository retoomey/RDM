#include "Timestamp.h"
#include "Log.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace rdm {

// Define the static constants
const Timestamp Timestamp::NONE{-1, -1};
const Timestamp Timestamp::ZERO{0, 0};
const Timestamp Timestamp::ENDT{0x7fffffff, 999999};

Timestamp Timestamp::Now() {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) < 0) {
        LogSyserr("gettimeofday failure in Timestamp::Now()");
        return Timestamp::ZERO;
    }
    return Timestamp::FromTimeval(tv);
}

Timestamp Timestamp::FromTimeval(const struct timeval& tv) {
    return Timestamp(tv.tv_sec, tv.tv_usec);
}

struct timeval Timestamp::ToTimeval() const {
    struct timeval tv;
    tv.tv_sec = tv_sec;
    tv.tv_usec = tv_usec;
    return tv;
}

// --- Math Operators ---

Timestamp Timestamp::operator+(const Timestamp& rhs) const {
    Timestamp result(tv_sec + rhs.tv_sec, tv_usec + rhs.tv_usec);
    if (result.tv_usec >= 1000000) {
        result.tv_sec += 1;
        result.tv_usec -= 1000000;
    }
    return result;
}

Timestamp Timestamp::operator-(const Timestamp& rhs) const {
    Timestamp result(tv_sec - rhs.tv_sec, tv_usec - rhs.tv_usec);
    if (result.tv_usec < 0) {
        if (result.tv_sec > 0) {
            result.tv_sec -= 1;
            result.tv_usec += 1000000;
        } else {
            result.tv_sec = 0;
            result.tv_usec = 0;
        }
    }
    return result;
}

Timestamp& Timestamp::operator+=(const Timestamp& rhs) {
    *this = *this + rhs;
    return *this;
}

Timestamp& Timestamp::operator-=(const Timestamp& rhs) {
    *this = *this - rhs;
    return *this;
}

// --- Comparison Operators ---

bool Timestamp::operator==(const Timestamp& rhs) const {
    return tv_sec == rhs.tv_sec && tv_usec == rhs.tv_usec;
}

bool Timestamp::operator!=(const Timestamp& rhs) const {
    return !(*this == rhs);
}

bool Timestamp::operator<(const Timestamp& rhs) const {
    if (tv_sec == rhs.tv_sec) return tv_usec < rhs.tv_usec;
    return tv_sec < rhs.tv_sec;
}

bool Timestamp::operator<=(const Timestamp& rhs) const {
    return *this < rhs || *this == rhs;
}

bool Timestamp::operator>(const Timestamp& rhs) const {
    return !(*this <= rhs);
}

bool Timestamp::operator>=(const Timestamp& rhs) const {
    return !(*this < rhs);
}

// --- Utilities ---

double Timestamp::AsSeconds() const {
    return static_cast<double>(tv_sec) + (static_cast<double>(tv_usec) / 1000000.0);
}

void Timestamp::IncrementMicrosecond() {
    if (tv_usec == 999999) {
        tv_usec = 0;
        tv_sec++;
    } else {
        tv_usec++;
    }
}

void Timestamp::DecrementMicrosecond() {
    if (tv_usec == 0) {
        tv_usec = 999999;
        tv_sec--;
    } else {
        tv_usec--;
    }
}

// --- String Operations ---

std::string Timestamp::ToString() const {
    if (*this == NONE) return "TS_NONE";
    if (*this == ZERO) return "TS_ZERO";
    if (*this == ENDT) return "TS_ENDT";

    struct tm tm_buf;
    time_t tsec = tv_sec;
    if (!gmtime_r(&tsec, &tm_buf)) return "";

    char buf[64];
    size_t nbytes = std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S.", &tm_buf);
    if (nbytes == 0) return "";

    std::snprintf(buf + nbytes, sizeof(buf) - nbytes, "%06d", static_cast<int>(tv_usec));
    return std::string(buf);
}

std::optional<Timestamp> Timestamp::Parse(const std::string& str) {
    int year, month, day, hour, minute, second;
    long microseconds = 0;

    int nfields = std::sscanf(str.c_str(), "%04d%02d%02dT%02d%02d%02d.%06ld",
                              &year, &month, &day, &hour, &minute, &second, &microseconds);

    if (nfields < 6 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 61) {
        return std::nullopt;
    }

    struct tm tm{};
    tzset(); // Ensure timezone is initialized
    tm.tm_isdst = 0;
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second - static_cast<int>(timezone);

    time_t sec = mktime(&tm);
    if (sec == -1) return std::nullopt;

    return Timestamp(sec, static_cast<int32_t>(microseconds));
}

}
