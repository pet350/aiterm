// Part of project: aiterm
// environment.h
// C functions for environment-variable configuration overrides
// By: Peter Talbott
// With assistance from OpenAI
// October 2026

/* aiterm environment-variable configuration overrides.
 * For a config key such as term_transparency, use AITERM_TERM_TRANSPARENCY.
 * Environment values take precedence over aiterm.conf and are never written
 * back to the configuration file. */

#include "environment.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *environment_get_config_override(const char *config_key)
{
    if (!config_key || !*config_key) return NULL;
    /* Provider credentials have their own plaintext environment handling. */
    gsize key_length = strlen(config_key);
    if (key_length >= 4 && strcmp(config_key + key_length - 4, "_KEY") == 0)
        return NULL;
    char *environment_name = g_strdup_printf("AITERM_%s", config_key);
    for (char *character = environment_name; *character; ++character)
        *character = (char)toupper((unsigned char)*character);
    const char *value = getenv(environment_name);
    g_free(environment_name);
    return value;
}

gboolean environment_apply_config_override(char *line, gsize line_size)
{
    if (!line || line_size == 0 || line[0] == '#' || !strchr(line, '='))
        return FALSE;
    char *equals = strchr(line, '=');
    gsize key_length = (gsize)(equals - line);
    if (key_length == 0) return FALSE;
    char *key = g_strndup(line, key_length);
    const char *value = environment_get_config_override(key);
    g_free(key);
    if (!value) return FALSE;
    /* db_pass is intentionally handled as plaintext from the environment. */
    if (strcmp(line, "db_pass=") == 0 || strncmp(line, "db_pass=", 8) == 0) {
        g_strlcpy(equals + 1, value, line_size - key_length - 1);
        return TRUE;
    }
    g_strlcpy(equals + 1, value, line_size - key_length - 1);
    return TRUE;
}
