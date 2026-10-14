/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl
 Copyright (C) 2003, 2004, 2005, 2006, 2007 StatPro Italia srl
 Copyright (C) 2004, 2005, 2006 Ferdinando Ametrano
 Copyright (C) 2006 Katiuscia Manzoni
 Copyright (C) 2006 Toyin Akin
 Copyright (C) 2015 Klaus Spanderen
 Copyright (C) 2020 Leonardo Arcari
 Copyright (C) 2020 Kline s.r.l.

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <https://www.quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/time/date.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/errors.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <functional>
#include <iomanip>
#include <ctime>

#ifdef QL_HIGH_RESOLUTION_DATE
#if BOOST_VERSION < 106700
#include <boost/functional/hash.hpp>
#else
#include <boost/container_hash/hash.hpp>
#endif
#endif

#if defined(BOOST_NO_STDC_NAMESPACE)
    namespace std { using ::time; using ::time_t; using ::tm;
                    using ::gmtime; using ::localtime; }
#endif

#ifdef QL_HIGH_RESOLUTION_DATE
using boost::posix_time::ptime;
using boost::posix_time::time_duration;
#endif

namespace {

    /* std::localtime returns a pointer into a shared static buffer, so it is
       neither reentrant nor safe to call from more than one thread; MSVC also
       flags it as deprecated.  Use the platform-specific reentrant variants
       where they exist, letting Boost's own platform matrix decide: a few
       platforms, MinGW among them, provide no localtime_r.  See
       boost/date_time/compiler_config.hpp.
    */
    bool localTime(std::time_t t, std::tm& result) {
#if defined(BOOST_MSVC)
        // Microsoft's localtime_s is not the one from Annex K of the C
        // standard: it takes the output buffer first and returns an error
        // code instead of a pointer.
        return localtime_s(&result, &t) == 0;
#elif defined(BOOST_DATE_TIME_HAS_REENTRANT_STD_FUNCTIONS)
        return localtime_r(&t, &result) != nullptr;
#else
        QL_DEPRECATED_DISABLE_WARNING
        const std::tm* lt = std::localtime(&t);
        QL_DEPRECATED_ENABLE_WARNING
        if (lt == nullptr)
            return false;
        result = *lt;
        return true;
#endif
    }

}


namespace QuantLib {
#ifndef QL_HIGH_RESOLUTION_DATE
    // constructors
    Date::Date()
    : serialNumber_(Date::serial_type(0)) {}

    Date::Date(Day d, Month m, Year y) {
        QL_REQUIRE(y > 1900 && y < 2200,
                   "year " << y << " out of bound. It must be in [1901,2199]");
        QL_REQUIRE(Integer(m) > 0 && Integer(m) < 13,
                   "month " << Integer(m)
                   << " outside January-December range [1,12]");

        bool leap = isLeap(y);
        Day len = monthLength(m,leap), offset = monthOffset(m,leap);
        QL_REQUIRE(d <= len && d > 0,
                   "day outside month (" << Integer(m) << ") day-range "
                   << "[1," << len << "]");

        serialNumber_ = d + offset + yearOffset(y);
    }

    Date& Date::operator+=(const Period& p) {
        serialNumber_ = advance(*this,p.length(),p.units()).serialNumber();
        return *this;
    }

    Date& Date::operator-=(const Period& p) {
        serialNumber_ = advance(*this,-p.length(),p.units()).serialNumber();
        return *this;
    }

    Date Date::advance(const Date& date, Integer n, TimeUnit units) {
        switch (units) {
          case Days:
            return date + n;
          case Weeks:
            return date + 7*n;
          case Months: {
            Day d = date.dayOfMonth();
            Integer m = Integer(date.month())+n;
            Year y = date.year();
            while (m > 12) {
                m -= 12;
                y += 1;
            }
            while (m < 1) {
                m += 12;
                y -= 1;
            }

            QL_ENSURE(y >= 1900 && y <= 2199,
                      "year " << y << " out of bounds. "
                      << "It must be in [1901,2199]");

            Integer length = monthLength(Month(m), isLeap(y));
            if (d > length)
                d = length;

            return {d, Month(m), y};
          }
          case Years: {
              Day d = date.dayOfMonth();
              Month m = date.month();
              Year y = date.year()+n;

              QL_ENSURE(y >= 1900 && y <= 2199,
                        "year " << y << " out of bounds. "
                        << "It must be in [1901,2199]");

              if (d == 29 && m == February && !isLeap(y))
                  d = 28;

              return {d, m, y};
          }
          default:
            QL_FAIL("undefined time units");
        }
    }

    bool Date::isLeap(Year y) {
        static const bool YearIsLeap[] = {
            // 1900 is leap in agreement with Excel's bug
            // 1900 is out of valid date range anyway
            // 1900-1909
             true,false,false,false, true,false,false,false, true,false,
            // 1910-1919
            false,false, true,false,false,false, true,false,false,false,
            // 1920-1929
             true,false,false,false, true,false,false,false, true,false,
            // 1930-1939
            false,false, true,false,false,false, true,false,false,false,
            // 1940-1949
             true,false,false,false, true,false,false,false, true,false,
            // 1950-1959
            false,false, true,false,false,false, true,false,false,false,
            // 1960-1969
             true,false,false,false, true,false,false,false, true,false,
            // 1970-1979
            false,false, true,false,false,false, true,false,false,false,
            // 1980-1989
             true,false,false,false, true,false,false,false, true,false,
            // 1990-1999
            false,false, true,false,false,false, true,false,false,false,
            // 2000-2009
             true,false,false,false, true,false,false,false, true,false,
            // 2010-2019
            false,false, true,false,false,false, true,false,false,false,
            // 2020-2029
             true,false,false,false, true,false,false,false, true,false,
            // 2030-2039
            false,false, true,false,false,false, true,false,false,false,
            // 2040-2049
             true,false,false,false, true,false,false,false, true,false,
            // 2050-2059
            false,false, true,false,false,false, true,false,false,false,
            // 2060-2069
             true,false,false,false, true,false,false,false, true,false,
            // 2070-2079
            false,false, true,false,false,false, true,false,false,false,
            // 2080-2089
             true,false,false,false, true,false,false,false, true,false,
            // 2090-2099
            false,false, true,false,false,false, true,false,false,false,
            // 2100-2109
            false,false,false,false, true,false,false,false, true,false,
            // 2110-2119
            false,false, true,false,false,false, true,false,false,false,
            // 2120-2129
             true,false,false,false, true,false,false,false, true,false,
            // 2130-2139
            false,false, true,false,false,false, true,false,false,false,
            // 2140-2149
             true,false,false,false, true,false,false,false, true,false,
            // 2150-2159
            false,false, true,false,false,false, true,false,false,false,
            // 2160-2169
             true,false,false,false, true,false,false,false, true,false,
            // 2170-2179
            false,false, true,false,false,false, true,false,false,false,
            // 2180-2189
             true,false,false,false, true,false,false,false, true,false,
            // 2190-2199
            false,false, true,false,false,false, true,false,false,false,
            // 2200
            false
        };
        QL_REQUIRE(y>=1900 && y<=2200, "year outside valid range");
        return YearIsLeap[y-1900];
    }


#else

    namespace {
        const boost::gregorian::date& serialNumberDateReference() {
            static const boost::gregorian::date dateReference(
                1899, boost::gregorian::Dec, 30);
            return dateReference;
        }


#define compatibleEnums (   int(boost::date_time::Monday)   +1 == Monday    \
                         && int(boost::date_time::Tuesday)  +1 == Tuesday   \
                         && int(boost::date_time::Wednesday)+1 == Wednesday \
                         && int(boost::date_time::Thursday) +1 == Thursday  \
                         && int(boost::date_time::Friday)   +1 == Friday    \
                         && int(boost::date_time::Saturday) +1 == Saturday  \
                         && int(boost::date_time::Sunday)   +1 == Sunday    \
                         && int(boost::date_time::Jan) == January           \
                         && int(boost::date_time::Feb) == February          \
                         && int(boost::date_time::Mar) == March             \
                         && int(boost::date_time::Apr) == April             \
                         && int(boost::date_time::May) == May               \
                         && int(boost::date_time::Jun) == June              \
                         && int(boost::date_time::Jul) == July              \
                         && int(boost::date_time::Aug) == August            \
                         && int(boost::date_time::Sep) == September         \
                         && int(boost::date_time::Oct) == October           \
                         && int(boost::date_time::Nov) == November          \
                         && int(boost::date_time::Dec) == December           )

        template <bool compatible>
        Weekday mapBoostDateType2QL(boost::gregorian::greg_weekday d) {
            if (compatible) {
                return Weekday(d.as_number() + 1);
            }
            else {
                switch (d) {
                  case boost::date_time::Monday   : return Monday;
                  case boost::date_time::Tuesday  : return Tuesday;
                  case boost::date_time::Wednesday: return Wednesday;
                  case boost::date_time::Thursday : return Thursday;
                  case boost::date_time::Friday   : return Friday;
                  case boost::date_time::Saturday : return Saturday;
                  case boost::date_time::Sunday   : return Sunday;
                  default:
                    QL_FAIL("unknown boost date_time day of week given");
                }
            }
        }

        template <bool compatible>
        Month mapBoostDateType2QL(boost::gregorian::greg_month m) {
            if (compatible) {
                return Month(m.as_number());
            }
            else {
                switch (m) {
                  case boost::date_time::Jan : return January;
                  case boost::date_time::Feb : return February;
                  case boost::date_time::Mar : return March;
                  case boost::date_time::Apr : return April;
                  case boost::date_time::May : return May;
                  case boost::date_time::Jun : return June;
                  case boost::date_time::Jul : return July;
                  case boost::date_time::Aug : return August;
                  case boost::date_time::Sep : return September;
                  case boost::date_time::Oct : return October;
                  case boost::date_time::Nov : return November;
                  case boost::date_time::Dec : return December;
                  default:
                    QL_FAIL("unknown boost date_time month of week given");
                }
            }
        }


        template <bool compatible>
        boost::gregorian::greg_month mapQLDateType2Boost(Month m) {
            if (compatible) {
                return boost::gregorian::greg_month(m);
            }
            else {
                switch (m) {
                  case January  : return boost::date_time::Jan;
                  case February : return boost::date_time::Feb;
                  case March    : return boost::date_time::Mar;
                  case April    : return boost::date_time::Apr;
                  case May      : return boost::date_time::May;
                  case June     : return boost::date_time::Jun;
                  case July     : return boost::date_time::Jul;
                  case August   : return boost::date_time::Aug;
                  case September: return boost::date_time::Sep;
                  case October  : return boost::date_time::Oct;
                  case November : return boost::date_time::Nov;
                  case December : return boost::date_time::Dec;
                  default:
                    QL_FAIL("unknown boost date_time month of week given");
                }
            }
        }

        void advance(ptime& dt, Integer n, TimeUnit units) {
            using boost::gregorian::gregorian_calendar;

            switch (units) {
              case Days:
                dt += boost::gregorian::days(n);
                break;
              case Weeks:
                dt += boost::gregorian::weeks(n);
                break;
              case Months:
              case Years : {
                  const boost::gregorian::date date = dt.date();
                  const Day eoM = gregorian_calendar::end_of_month_day(
                      date.year(), date.month());

                  if (units == Months) {
                      dt += boost::gregorian::months(n);
                  }
                  else {
                      dt += boost::gregorian::years(n);
                  }

                  if (date.day() == eoM) {
                      // avoid snap-to-end-of-month
                      // behavior of boost::date_time
                      const Day newEoM
                            = gregorian_calendar::end_of_month_day(
                                  dt.date().year(), dt.date().month());

                      if (newEoM > eoM) {
                          dt -= boost::gregorian::days(newEoM - eoM);
                      }
                  }
                 }
                break;
              case Hours:
                dt += boost::posix_time::hours(n);
                break;
              case Minutes:
                  dt += boost::posix_time::minutes(n);
                  break;
              case Seconds:
                dt += boost::posix_time::seconds(n);
                break;
              case Milliseconds:
                dt += boost::posix_time::milliseconds(n);
                break;
              case Microseconds:
                dt += boost::posix_time::microseconds(n);
                break;
              default:
                QL_FAIL("undefined time units");
           }
        }

        boost::gregorian::date gregorianDate(Year y, Month m, Day d) {
            QL_REQUIRE(y > 1900 && y < 2200,
                       "year " << y << " out of bound. It must be in [1901,2199]");
            QL_REQUIRE(Integer(m) > 0 && Integer(m) < 13,
                       "month " << Integer(m)
                       << " outside January-December range [1,12]");

            const boost::gregorian::greg_month bM
                = mapQLDateType2Boost<compatibleEnums>(m);

            const Day len =
                boost::gregorian::gregorian_calendar::end_of_month_day(y, bM);
            QL_REQUIRE(d <= len && d > 0,
                       "day outside month (" << Integer(m) << ") day-range "
                       << "[1," << len << "]");

            return boost::gregorian::date(y, bM, d);
        }
    }


    Date::Date()
    : dateTime_(serialNumberDateReference()) {}

    Date::Date(const ptime& dateTime)
    : dateTime_(dateTime) {}

    Date::Date(Day d, Month m, Year y)
    : dateTime_(gregorianDate(y, m, d)) {}

    Date::Date(Day d, Month m, Year y,
               Hour hours, Minute minutes, Second seconds,
               Millisecond millisec, Microsecond microsec)
    : dateTime_(
         gregorianDate(y, m, d),
         boost::posix_time::time_duration(
             hours, minutes, seconds,
               millisec*(time_duration::ticks_per_second()/1000)
             + microsec*(time_duration::ticks_per_second()/1000000))) {}

    Date::Date(Date::serial_type serialNumber)
    : dateTime_(
         serialNumberDateReference() +
         boost::gregorian::days(serialNumber)) {
        checkSerialNumber(serialNumber);
    }

    Weekday Date::weekday() const {
        return mapBoostDateType2QL<compatibleEnums>(
            dateTime_.date().day_of_week());
    }

    Day Date::dayOfMonth() const {
        return dateTime_.date().day();
    }

    Day Date::dayOfYear() const {
        return dateTime_.date().day_of_year();
    }

    Month Date::month() const {
        return mapBoostDateType2QL<compatibleEnums>(dateTime_.date().month());
    }

    Year Date::year() const {
        return dateTime_.date().year();
    }

    Hour Date::hours() const {
        return dateTime_.time_of_day().hours();
    }

    Minute Date::minutes() const {
        return dateTime_.time_of_day().minutes();
    }

    Second Date::seconds() const {
        return dateTime_.time_of_day().seconds();
    }

    Time Date::fractionOfDay() const {
        const time_duration t = dateTime().time_of_day();

        const Time seconds
            = (t.hours()*60.0 + t.minutes())*60.0 + t.seconds()
                + Real(t.fractional_seconds())/ticksPerSecond();

        return seconds/86400.0; // ignore any DST hocus-pocus
    }

    Time Date::fractionOfSecond() const {
        return dateTime_.time_of_day().fractional_seconds()
            /Real(ticksPerSecond());
    }

    Millisecond Date::milliseconds() const {
        return dateTime_.time_of_day().fractional_seconds()
               /(ticksPerSecond()/1000);
    }

    Microsecond Date::microseconds() const {
        return (dateTime_.time_of_day().fractional_seconds()
                - milliseconds()*(time_duration::ticks_per_second()/1000))
            /(ticksPerSecond()/1000000);
    }

    time_duration::tick_type Date::ticksPerSecond() {
        return time_duration::ticks_per_second();
    }

    Date::serial_type Date::serialNumber() const {
        const Date::serial_type n =
            (dateTime_.date() - serialNumberDateReference()).days();
        if (n != 0)
            checkSerialNumber(n);
        return n;
    }

    const ptime& Date::dateTime() const { return dateTime_; }

    Date& Date::operator+=(Date::serial_type d) {
        dateTime_ += boost::gregorian::days(d);
        return *this;
    }

    Date& Date::operator+=(const Period& p) {
        advance(dateTime_, p.length(), p.units());
        return *this;
    }

    Date& Date::operator-=(Date::serial_type d) {
        dateTime_ -= boost::gregorian::days(d);
        return *this;
    }
    Date& Date::operator-=(const Period& p) {
        advance(dateTime_, -p.length(), p.units());
        return *this;
    }

    Date& Date::operator++() {
        dateTime_ +=boost::gregorian::days(1);
        return *this;
    }

    Date& Date::operator--() {
        dateTime_ -=boost::gregorian::days(1);
        return *this;
    }

    Date Date::operator+(Date::serial_type days) const {
        Date retVal(*this);
        retVal+=days;

        return retVal;
    }

    Date Date::operator-(Date::serial_type days) const {
        Date retVal(*this);
        retVal-=days;

        return retVal;
    }

    Date Date::operator+(const Period& p) const {
        Date retVal(*this);
        retVal+=p;

        return retVal;
    }

    Date Date::operator-(const Period& p) const {
        Date retVal(*this);
        retVal-=p;

        return retVal;
    }

    Date Date::localDateTime() {
        return Date(boost::posix_time::microsec_clock::local_time());
    }

    Date Date::universalDateTime() {
        return Date(boost::posix_time::microsec_clock::universal_time());
    }

    bool Date::isLeap(Year y) {
        return boost::gregorian::gregorian_calendar::is_leap_year(y);
    }

    Date Date::endOfMonth(const Date& d) {
        const Month m = d.month();
        const Year y = d.year();
        const Day eoM = boost::gregorian::gregorian_calendar::end_of_month_day(
            d.year(), mapQLDateType2Boost<compatibleEnums>(d.month()));

        return Date(eoM, m, y);
    }

    bool Date::isEndOfMonth(const Date& d) {
        return d.dayOfMonth() ==
            boost::gregorian::gregorian_calendar::end_of_month_day(
                d.year(), mapQLDateType2Boost<compatibleEnums>(d.month()));
    }


    Date::serial_type operator-(const Date& d1, const Date& d2) {
        return (d1.dateTime().date() - d2.dateTime().date()).days();
    }

    Time daysBetween(const Date& d1, const Date& d2) {
        const Date::serial_type days = d2 - d1;
        return days + d2.fractionOfDay() - d1.fractionOfDay();
    }

    bool operator<(const Date& d1, const Date& d2) {
        return (d1.dateTime() < d2.dateTime());
    }

    bool operator<=(const Date& d1, const Date& d2) {
        return (d1.dateTime() <= d2.dateTime());
    }

    bool operator>(const Date& d1, const Date& d2) {
        return (d1.dateTime() > d2.dateTime());
    }

    bool operator>=(const Date& d1, const Date& d2) {
        return (d1.dateTime() >= d2.dateTime());
    }

    bool operator==(const Date& d1, const Date& d2) {
        return (d1.dateTime() == d2.dateTime());
    }

    bool operator!=(const Date& d1, const Date& d2) {
        return (d1.dateTime() != d2.dateTime());
    }
#endif

    void Date::failSerialNumber(Date::serial_type serialNumber) {
        QL_FAIL("Date's serial number (" << serialNumber << ") outside "
                "allowed range [" << minimumSerialNumber() <<
                "-" << maximumSerialNumber() << "], i.e. [" <<
                minDate() << "-" << maxDate() << "]");
    }

    Date Date::minDate() {
        static const Date minimumDate(minimumSerialNumber());
        return minimumDate;
    }

    Date Date::maxDate() {
        static const Date maximumDate(maximumSerialNumber());
        return maximumDate;
    }

    Date Date::operator++(int ) {
        Date old(*this);
        ++*this; // use the pre-increment
        return old;
    }

    Date Date::operator--(int ) {
        Date old(*this);
        --*this; // use the pre-decrement
        return old;
    }

    Date Date::todaysDate() {
        std::time_t t;

        if (std::time(&t) == std::time_t(-1)) // -1 means time() didn't work
            return {};

        std::tm lt{};
        if (!localTime(t, lt)) // the conversion failed
            return {};

        return {Day(lt.tm_mday), Month(lt.tm_mon + 1), Year(lt.tm_year + 1900)};
    }

    Date Date::nextWeekday(const Date& d, Weekday dayOfWeek) {
        Weekday wd = d.weekday();
        return d + ((wd>dayOfWeek ? 7 : 0) - wd + dayOfWeek);
    }

    Date Date::nthWeekday(Size nth, Weekday dayOfWeek,
                          Month m, Year y) {
        QL_REQUIRE(nth>0,
                   "zeroth day of week in a given (month, year) is undefined");
        QL_REQUIRE(nth<6,
                   "no more than 5 weekday in a given (month, year)");
        Weekday first = Date(1, m, y).weekday();
        Size skip = nth - (dayOfWeek>=first ? 1 : 0);
        return {Day((1 + dayOfWeek + skip * 7) - first), m, y};
    }

    // month formatting

    std::ostream& operator<<(std::ostream& out, Month m) {
        switch (m) {
          case January:
            return out << "January";
          case February:
            return out << "February";
          case March:
            return out << "March";
          case April:
            return out << "April";
          case May:
            return out << "May";
          case June:
            return out << "June";
          case July:
            return out << "July";
          case August:
            return out << "August";
          case September:
            return out << "September";
          case October:
            return out << "October";
          case November:
            return out << "November";
          case December:
            return out << "December";
          default:
            QL_FAIL("unknown month (" << Integer(m) << ")");
        }
    }

    std::size_t hash_value(const Date& d) {
#ifdef QL_HIGH_RESOLUTION_DATE
        std::size_t seed = 0;
        boost::hash_combine(seed, d.serialNumber());
        boost::hash_combine(seed, d.dateTime().time_of_day().total_nanoseconds());
        return seed;
#else
        return std::hash<Date::serial_type>()(d.serialNumber());
#endif
    }

    // date formatting

    std::ostream& operator<<(std::ostream& out, const Date& d) {
        return out << io::long_date(d);
    }

    namespace detail {

        struct FormatResetter { // NOLINT(cppcoreguidelines-special-member-functions)
            // An instance of this object will have undefined behaviour
            // if the object out passed in the constructor is destroyed
            // before this instance
            struct nopunct : std::numpunct<char> {
                std::string do_grouping() const override { return ""; }
            };
            explicit FormatResetter(std::ostream &out)
                : out_(&out), flags_(out.flags()), filler_(out.fill()),
                  loc_(out.getloc()) {
                std::locale loc (out.getloc(),new nopunct);
                out.imbue(loc);
                out << std::resetiosflags(
                    std::ios_base::adjustfield | std::ios_base::basefield |
                    std::ios_base::floatfield | std::ios_base::showbase |
                    std::ios_base::showpos | std::ios_base::uppercase);
                out << std::right;
            }
            ~FormatResetter() {
                out_->flags(flags_);
                out_->fill(filler_);
                out_->imbue(loc_);
            }
            std::ostream *out_;
            std::ios_base::fmtflags flags_;
            char filler_;
            std::locale loc_;
        };

        std::ostream& operator<<(std::ostream& out,
                                 const short_date_holder& holder) {
            const Date& d = holder.d;
            if (d == Date()) {
                out << "null date";
            } else {
                FormatResetter resetter(out);
                Integer dd = d.dayOfMonth(), mm = Integer(d.month()),
                        yyyy = d.year();
                char filler = out.fill();
                out << std::setw(2) << std::setfill('0') << mm << "/";
                out << std::setw(2) << std::setfill('0') << dd << "/";
                out << yyyy;
                out.fill(filler);
            }
            return out;
        }

        std::ostream& operator<<(std::ostream& out,
                                 const long_date_holder& holder) {
            const Date& d = holder.d;
            if (d == Date()) {
                out << "null date";
            } else {
                FormatResetter resetter(out);
                out << d.month() << " ";
                out << io::ordinal(d.dayOfMonth()) << ", ";
                out << d.year();
            }
            return out;
        }

        std::ostream& operator<<(std::ostream& out,
                                 const iso_date_holder& holder) {
            const Date& d = holder.d;
            if (d == Date()) {
                out << "null date";
            } else {
                FormatResetter resetter(out);
                Integer dd = d.dayOfMonth(), mm = Integer(d.month()),
                        yyyy = d.year();
                out << yyyy << "-";
                out << std::setw(2) << std::setfill('0') << mm << "-";
                out << std::setw(2) << std::setfill('0') << dd;
            }
            return out;
        }

        std::ostream& operator<<(std::ostream& out,
                                 const formatted_date_holder& holder) {
            using namespace boost::gregorian;
            const Date& d = holder.d;
            if (d == Date()) {
                out << "null date";
            } else {
                FormatResetter resetter(out);
                date boostDate(d.year(), d.month(), d.dayOfMonth());
                out.imbue(std::locale(std::locale(),
                                      new date_facet(holder.f.c_str())));
                out << boostDate;
            }
            return out;
        }

#ifdef QL_HIGH_RESOLUTION_DATE
        std::ostream& operator<<(std::ostream& out,
                                 const iso_datetime_holder& holder) {
            const Date& d = holder.d;

            out << io::iso_date(d) << "T";
            FormatResetter resetter(out);
            const Hour hh= d.hours();
            const Minute mm = d.minutes();
            const Second s = d.seconds();
            const Millisecond millis = d.milliseconds();
            const Microsecond micros = d.microseconds();

            out << std::setw(2) << std::setfill('0') << hh << ":"
                << std::setw(2) << std::setfill('0') << mm << ":"
                << std::setw(2) << std::setfill('0') << s << ","
                << std::setw(3) << std::setfill('0') << millis
                << std::setw(3) << std::setfill('0') << micros;

            return out;
        }
#endif
    }

    namespace io {
        detail::short_date_holder short_date(const Date& d) {
            return detail::short_date_holder(d);
        }

        detail::long_date_holder long_date(const Date& d) {
            return detail::long_date_holder(d);
        }

        detail::iso_date_holder iso_date(const Date& d) {
            return detail::iso_date_holder(d);
        }

        detail::formatted_date_holder formatted_date(const Date& d,
                                                     const std::string& f) {
            return detail::formatted_date_holder(d, f);
        }

#ifdef QL_HIGH_RESOLUTION_DATE
        detail::iso_datetime_holder iso_datetime(const Date& d) {
            return detail::iso_datetime_holder(d);
        }
#endif
    }
}
