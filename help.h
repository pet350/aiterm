#ifndef HELP_H
#define HELP_H

// Returns the static help menu string
const char* get_help_text();

// Returns a dynamically allocated string with HW stats
// Caller must free() the result.
char* get_hw_stats();

// Returns the version string
const char* get_version_info();

#endif
