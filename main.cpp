/* Copyright 2022 Peter Luick

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include <iostream>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <exception>
#include <cstdio>
#include <vector>
#include <regex>
#include <algorithm>
#include <iterator>

const char *usage = R"HERE(
usage: filetimegen <spec> [OPTIONS]

BRIEF:
Outputs a filename according to <spec>. Typically this is a prefix plus a time.
If --prune is given, a list of specs will be taken on stdin, and a list of
specs that should be discarded is output.

REQUIRED ARGUMENTS:
<spec>           Specifies how the output should be named. Will replace any
                 instance of {now} with the current time. If spec does not
                 contain {now} anywhere, this command will fail.

[OPTIONS]
    -h, --help   Print this message.
    --prune      Changes the mode of the command so that it expects a list of
                 files to be provided on stdin (null seperated). This will
                 output the list of files that should be deleted based on
                 --keep specifications.
    --newline    When printing and accepting input, use newlines instead of
                 null seperators.
    -M, --keep-minutely
    -H, --keep-hourly
    -d, --keep-daily
    -w, --keep-weekly
    -m, --keep-monthly
                 Specifiers for how many files should be kept. Only used during
                 --prune operation.
)HERE";


using std::shared_ptr;
using std::string;
using std::cout;
using std::vector;
using std::chrono::system_clock;


class CLArgs {
public:
	string Spec;
	shared_ptr<int> KeepMinutely;
	shared_ptr<int> KeepHourly;
	shared_ptr<int> KeepDaily;
	shared_ptr<int> KeepWeekly;
	shared_ptr<int> KeepMonthly;
	bool Newline;
	bool Prune;

	CLArgs() =default;
	CLArgs(int argc, char **argv);

private:
	int ParseCLInt(int &i, int argc, char **argv);
	void ValidateArgs();
};

bool streq(string const& lhs, const char* rhs)
{
	return lhs.compare(rhs) == 0;
}

/* i is pointing at the current option, need to increment before parsing.
 */
int CLArgs::ParseCLInt(int &i, int argc, char **argv)
{
	string errstring = string("option '") + argv[i] + "' requires a numeric argument";
	i++;
	if (i >= argc)
		throw std::invalid_argument(errstring);

	try {
		return std::stoi(argv[i]);
	}
	catch (...) {
		throw std::invalid_argument(errstring);
	}
}

CLArgs::CLArgs(int argc, char **argv)
	: Spec(""),
	KeepMinutely(nullptr),
	KeepHourly(nullptr),
	KeepDaily(nullptr),
	KeepWeekly(nullptr),
	KeepMonthly(nullptr),
	Newline(false),
	Prune(false)
{
	size_t posargs = 0;
	for (int i = 1; i < argc; i++) {
		string arg(argv[i]);
		if (streq(arg, "-h") || streq(arg, "--help")) {
			cout << usage;
			throw std::invalid_argument("");
		}
		else if (streq(arg, "--newline"))
			Newline = true;
		else if (streq(arg, "--prune"))
			Prune = true;
		else if (streq(arg, "-M") || streq(arg, "--keep-minutely"))
			KeepMinutely = std::make_shared<int>(ParseCLInt(i, argc, argv));
		else if (streq(arg, "-H") || streq(arg, "--keep-hourly"))
			KeepHourly = std::make_shared<int>(ParseCLInt(i, argc, argv));
		else if (streq(arg, "-d") || streq(arg, "--keep-daily"))
			KeepDaily = std::make_shared<int>(ParseCLInt(i, argc, argv));
		else if (streq(arg, "-w") || streq(arg, "--keep-weekly"))
			KeepWeekly = std::make_shared<int>(ParseCLInt(i, argc, argv));
		else if (streq(arg, "-m") || streq(arg, "--keep-monthly"))
			KeepMonthly = std::make_shared<int>(ParseCLInt(i, argc, argv));
		else if (posargs == 0) {
			Spec = arg;
			posargs++;
		}
		else
			throw std::invalid_argument(string("invalid argument: ") + arg);
	}
	ValidateArgs();
}

void CLArgs::ValidateArgs()
{
	if ((KeepMinutely && *KeepMinutely < 1)
			|| (KeepHourly && *KeepHourly < 1)
			|| (KeepDaily && *KeepDaily < 1)
			|| (KeepWeekly && *KeepWeekly < 1)
			|| (KeepMonthly && *KeepMonthly < 1)
			)
	{
		throw std::invalid_argument("All --keep arguments must be >= 1");
	}
	if (Spec.find("{now}") == string::npos)
		throw std::invalid_argument("<spec> must contain {now} somewhere");
}


/* Time comparison mask values. Specifies what needs to be checked when comparing time values
 */
enum timecomp : uint64_t {
	MINUTES   = 1 << 1,
	HOURS     = 1 << 2,
	MONTHDAYS = 1 << 3,
	MONTHS    = 1 << 4,
	YEARS     = 1 << 5,
	WEEKS     = 1 << 6,

	COMP_YEARLY = YEARS,
	COMP_MONTHLY = COMP_YEARLY | MONTHS,
	COMP_DAILY = COMP_MONTHLY | MONTHDAYS,
	COMP_HOURLY = COMP_DAILY | HOURS,
	COMP_MINUTELY = COMP_HOURLY | MINUTES,
	// Weeks can't be defined in the same cascading fashion.
	COMP_WEEKLY = YEARS | WEEKS,
};

struct timestruct {
	int sec;  // seconds after the minute (0-60)
	int min;  // minutes after the hour (0-59)
	int hour; // hours since midnight (0-23)
	int mday; // day of the month (1-31)
	int mon;  // month (1-12)
	int year; // year
	int yday; // day of year (0-365)
	int week; // week of year (0-52)

	system_clock::time_point tp;

	timestruct(system_clock::time_point time_point);
	timestruct(string const& intime);

	bool EqlMask(timestruct const& rhs, uint64_t mask);
};

/* NOTE: struct tm has a few unique properties, such as tm_year being defined as "years since 1900".
 * These are noted in the constructor below.
 */
timestruct::timestruct(system_clock::time_point time_point)
	: tp(time_point)
{
	std::time_t now_tt = system_clock::to_time_t(tp);
	tm *tm_time = localtime(&now_tt);

	sec = tm_time->tm_sec;
	min = tm_time->tm_min;
	hour = tm_time->tm_hour;
	mday = tm_time->tm_mday;
	mon = tm_time->tm_mon + 1; // tm_mon is months since January (0-11)
	year = tm_time->tm_year + 1900; // tm_year is years since 1900
	yday = tm_time->tm_yday; // tm_yday is days since January 1 (0-365)
	week = tm_time->tm_yday / 7; // Not the ISO 8601 weekly calendar, but it's good enough for backups
}

timestruct::timestruct(string const& intime)
{
	// Regex to extract the date fields
	std::regex nowre(R"HERE((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))HERE");
	std::smatch sm;
	if (!std::regex_match(intime.cbegin(), intime.cend(), sm, nowre))
		throw std::invalid_argument("{now} is not the correct time format");
	try {
		// the first match is the entire regex match, subsequent values are capture groups. C++ std.
		year = std::stoi(sm[1]);
		mon = std::stoi(sm[2]);
		mday = std::stoi(sm[3]);
		hour = std::stoi(sm[4]);
		min = std::stoi(sm[5]);
		sec = std::stoi(sm[6]);
	}
	catch (...) {
		throw std::invalid_argument("failed to convert to valid time");
	}

	// Convert fields to get a valid time_point. This is an inverse of the operations in the other
	// constructor.
	tm timeinfo;
	timeinfo.tm_sec = sec;
	timeinfo.tm_min = min;
	timeinfo.tm_hour = hour;
	timeinfo.tm_mday = mday;
	timeinfo.tm_mon = mon - 1;
	timeinfo.tm_year = year - 1900;
	// We will assume not daylight savings time. This is a bug, but ISO 8601 doesn't have a good way
	// to show DST in the timespec.
	timeinfo.tm_isdst = 0;
	std::time_t tt = mktime(&timeinfo);
	tp = system_clock::from_time_t(tt);
	// Fill in these fields after calling mktime
	yday = timeinfo.tm_yday;
	week = timeinfo.tm_yday / 7;
}

// Mask is actually a selection mask, so if the bit is set that comparison will matter
bool timestruct::EqlMask(timestruct const& rhs, uint64_t mask)
{
	bool retval = true;
	if (mask & MINUTES)
		retval = retval && (min == rhs.min);
	if (mask & HOURS)
		retval = retval && (hour == rhs.hour);
	if (mask & MONTHDAYS)
		retval = retval && (mday == rhs.mday);
	if (mask & MONTHS)
		retval = retval && (mon == rhs.mon);
	if (mask & YEARS)
		retval = retval && (year == rhs.year);
	if (mask & WEEKS)
		retval = retval && (week == rhs.week);
	return retval;
}

bool SortTimestruct(timestruct const& lhs, timestruct const& rhs)
{
	return lhs.tp < rhs.tp;
}

// length of "2020-01-12T13:45:00"
const size_t NOW_SPEC_LENGTH = 19;

string GenerateFileTime(string Spec, timestruct now)
{
	char now_str[100];
	std::snprintf(now_str, 100, "%04d-%02d-%02dT%02d:%02d:%02d",
			now.year, now.mon, now.mday,
			now.hour, now.min, now.sec
			);

	size_t pos;
	while ((pos = Spec.find("{now}")) != string::npos)
		Spec.replace(pos, 5, now_str);
	return Spec;
}


bool ValidateInputSpec(string const& Spec, string const& line, vector<size_t> const& nowpos)
{
	size_t now_i = 0;
	size_t spec_i = 0;
	size_t line_i = 0;
	while (spec_i < Spec.size() && line_i < line.size()) {
		if (now_i < nowpos.size() && nowpos[now_i] == spec_i) {
			// Reached a {now} in the spec. Just verify character length.
			now_i += 1;
			spec_i += 5; // length of "{now}"
			line_i += NOW_SPEC_LENGTH; // length of "2020-01-12T13:45:00"
		}
		else if (Spec[spec_i] != line[line_i]) {
			// Constant characters in the spec do not match
			return true;
		}
		else {
			// Constant character matches, just increment positions
			spec_i++;
			line_i++;
		}
	}
	if (now_i != nowpos.size() || spec_i != Spec.size() || line_i != line.size()) {
		return true;
	}
	return false;
}


void FindPruneKeep(vector<timestruct> const& times, vector<size_t> &keep, shared_ptr<int> keep_amt,
		uint64_t time_compmask)
{
	if (!keep_amt || times.empty())
		return;
	size_t total_keep = *keep_amt;
	vector<size_t> add_keep;
	add_keep.push_back(0); // always keep the most recent.
	timestruct current_keep = times[0];
	for (size_t i = 1; i < times.size() && add_keep.size() < total_keep; i++) {
		// If the current value is equal to the last timestamp kept, do not keep.
		if (current_keep.EqlMask(times[i], time_compmask))
			continue;
		current_keep = times[i];
		add_keep.push_back(i);
	}

	// Merge with keep
	vector<size_t> merged_keep;
	auto it = std::set_union(keep.begin(), keep.end(), add_keep.begin(), add_keep.end(),
			std::inserter(merged_keep, merged_keep.begin()));
	keep.swap(merged_keep);
}

void PruneFiles(CLArgs const& clargs)
{
	vector<size_t> nowpos;
	size_t pos = 0;
	while ((pos = clargs.Spec.find("{now}", pos)) != string::npos)
		nowpos.push_back(pos++);

	vector<timestruct> input_times;
	// Read from stdin
	char delim = clargs.Newline ? '\n' : '\0';
	for (string line; std::getline(std::cin, line, delim);) {
		if (ValidateInputSpec(clargs.Spec, line, nowpos)) {
			std::cerr << "warn: spec does not match input: " << line << "\n";
			continue;
		}

		try {
			// Only use the first {now} as the official timestamp
			input_times.push_back(timestruct(string(line.data() + nowpos[0], NOW_SPEC_LENGTH)));
		}
		catch (std::invalid_argument const& e) {
			std::cerr << "warn: in input '" << line << "': " << e.what() << "\n";
			continue;
		}
	}

	if (input_times.empty())
		return;

	// Sort from most recent to least recent
	std::sort(input_times.begin(), input_times.end(), SortTimestruct);
	std::reverse(input_times.begin(), input_times.end());

	// Figure out what to keep based on input
	vector<size_t> keep;
	keep.push_back(0); // always keep most recent
	FindPruneKeep(input_times, keep, clargs.KeepMinutely, COMP_MINUTELY);
	FindPruneKeep(input_times, keep, clargs.KeepHourly, COMP_HOURLY);
	FindPruneKeep(input_times, keep, clargs.KeepDaily, COMP_DAILY);
	FindPruneKeep(input_times, keep, clargs.KeepWeekly, COMP_WEEKLY);
	FindPruneKeep(input_times, keep, clargs.KeepMonthly, COMP_MONTHLY);

	// Output what should be pruned
	for (size_t i = 0; i < input_times.size(); i++) {
		if (!std::binary_search(keep.begin(), keep.end(), i)) {
			// Prune it
			cout << GenerateFileTime(clargs.Spec, input_times[i]) << delim;
		}
	}
}


int main(int argc, char **argv)
{
	CLArgs clargs;
	try {
		clargs = CLArgs(argc, argv);
	}
	catch (std::invalid_argument const& e) {
		std::cerr << e.what() << "\n";
		return 1;
	}

	if (clargs.Prune)
		PruneFiles(clargs);
	else
		cout << GenerateFileTime(clargs.Spec, timestruct(system_clock::now()));

	return 0;
}
