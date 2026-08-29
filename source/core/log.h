#pragma once

#include <string>

namespace nxp {

// Everything goes to stdout, which nxlink picks up during development.
//
// The file at sdmc:/switch/nx-plaza/plaza.log is **off by default** and is
// turned on in Settings. A released build should not be writing to somebody's
// SD card every twenty seconds for the life of the session, and the log names
// the server, the handles it sees and the console's own id - none of which
// wants to be lying in a file nobody asked for.
void logInit();
void logExit();
void logPrint(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Opens or closes the log file. Safe to call at any time, from any thread.
//
// Called once at startup with the stored setting, and again whenever the
// setting is toggled. Turning it on flushes the lines from before the settings
// were loaded, so enabling the log and relaunching captures the whole of a
// failing startup rather than starting from wherever the store finished.
void logSetFileEnabled(bool on);
bool logFileEnabled();

// Where the file is, for the Settings screen to show.
std::string logFilePath();

} // namespace nxp

#define LOG(...) ::nxp::logPrint(__VA_ARGS__)
