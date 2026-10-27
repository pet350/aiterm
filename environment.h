// Part of project: aiterm
// environment.h
// Header file for environment-variable configuration overrides
// By: Peter Talbott
// With assistance from OpenAI
// October 2026

#ifndef AITERM_ENVIRONMENT_H
#define AITERM_ENVIRONMENT_H

#include <glib.h>

// Function Prototypes

gboolean environment_apply_config_override(char *line, gsize line_size);
const char *environment_get_config_override(const char *config_key);

#endif
