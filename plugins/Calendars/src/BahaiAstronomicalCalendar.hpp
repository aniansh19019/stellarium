/*
 * Copyright (C) 2022 Georg Zotti
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#ifndef BAHAIASTRONOMICALCALENDAR_HPP
#define BAHAIASTRONOMICALCALENDAR_HPP

#include "BahaiArithmeticCalendar.hpp"

//! @class BahaiAstronomicalCalendar
//! @brief Functions for the Bahá´á Astronomical calendar
//! @author Georg Zotti
//! @ingroup calendars
//! The Bahá´í faith, founded in 1844, uses its own calendar, based on the number 19.
//! Until 2015 the calendar was based on the Gregorian calendar, then a more accurate astronomical calendar was decreed. This is the version implemented here.

class BahaiAstronomicalCalendar : public BahaiArithmeticCalendar
{
	Q_OBJECT

public:
	BahaiAstronomicalCalendar(double jd);

	~BahaiAstronomicalCalendar() override {}

public slots:
	//void retranslate() override;

	//! Set a calendar date from the Julian day number
	void setJD(double JD) override;

	//! set date from a vector of calendar date elements sorted from the largest to the smallest.
	//! Year-Month[1...12]-Day[1...31]
	void setDate(const QVector<int> &parts) override;

	//! get a stringlist of calendar date elements sorted from the largest to the smallest.
	//! Year, Month, MonthName, Day, DayName
	QStringList getDateStrings() const override;

//	//! get a formatted complete string for a date
//	QString getFormattedDateString() const override;

	//! Return moment of sunset in Tehran as defined by Bahai astronomical calendar rules. (CC:UE 16.6)
	static double bahaiSunset(int rd);

	//! Return RD of New year  (CC:UE 16.7)
	static int astroBahaiNewYearOnOrBefore(int rd);

	//! Return R.D. of date given in the Bahai Astronomical calendar. (CC:UE 16.8)
	static int fixedFromBahaiAstronomical(const QVector<int> &bahai5);

	//! Return R.D. of date given in the Bahai Astronomical calendar. (CC:UE 16.9)
	//! return major-cycle-year-month-day for RD date
	static QVector<int> bahaiAstronomicalFromFixed(int rd);

	//! Return R.D. of new year date in the Bahai Astronomical calendar for the given Gregorian year. (CC:UE 16.11)
	static int nawRuz(int gYear);

	//! Return R.D. of Ridvan in the Bahai Astronomical calendar for the given Gregorian year. (CC:UE 16.12)
	static int feastOfRidvan(int gYear);

	//! Return R.D. of the birth of the Bab in the Bahai Astronomical calendar for the given Gregorian year. (CC:UE 16.13)
	static int birthOfTheBab(int gYear);

private:
	static const StelLocation bahaiLocation;
};

#endif
