// part of aiterm project
// config.c
// Various utilities used in this project
// By: Peter Talbott
// Assisted by: Gemini
// May 2026, june 2026, july 2026, August 2026

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <json-c/json.h>
#include <vte/vte.h>
#include <mariadb/mysql.h>

#include "utils.h"
#include "gui.h"
#include "openai.h"
#include "crypto.h" // Add this include
#include "update.h"
#include "tee_handler.h"
#include "ratelimit.h"
#include "commands.h"
#include "noisefilter.h"
#include "config.h"
#include "menu.h"

static const char *CONFIG_FILE_VERSION="1.2";

static char* LOADED_PREFIX(AppContext *app) {
    int len = 64;
    char *out = g_malloc(len);
    snprintf(out, len, "[%s DEBUG %s]: [%sLoaded%s]%s",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan, app->ansi.normal, app->ansi.yellow);
    return out;
}

// Return ansi colored debug and decrypted
static char* DECRYPTED_PREFIX(AppContext *app) {
    int len = 128;
    char *out = g_malloc(len);
    snprintf(out, len, "[%s DEBUG %s]: [%sDECRYPTED%s]  [%sAES_256_CBC%s] %s",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan, app->ansi.normal, app->ansi.lt_red, app->ansi.normal, app->ansi.yellow);
    return out;
}

// Return ansi colored debug override
static char* OVERRIDE_PREFIX(AppContext *app) {
    int len = 128;
    char *out = g_malloc(len);
    snprintf(out, len, "[%s DEBUG %s]: [%sOVERRIDE%s] %s",
        app->ansi.lt_purple, app->ansi.normal, app->ansi.lt_red, app->ansi.normal, app->ansi.yellow); 
    return out;
}

// Return ansi green "On"
static char* ON_VAL(AppContext *app) {
    int len = 32;
    char *out = g_malloc(len);
    snprintf(out, len, "%sOn%s", app->ansi.green, app->ansi.yellow);
    return out;
}

// Return ansi red "Off"
static char* OFF_VAL(AppContext *app) {
    int len = 32;
    char *out = g_malloc(len);
    snprintf(out, len, "%sOff%s", app->ansi.red, app->ansi.yellow);
    return out;
}

static char* SKIP_VAL(AppContext *app) {
    int len = 128;
    char *out = g_malloc(len);
    snprintf(out, len, "[%s DEBUG %s]: [%sSKIP%s] %s",
	app->ansi.lt_purple, app->ansi.normal, app->ansi.cyan, app->ansi.normal, app->ansi.yellow);
    return out;
}

// Function to save config file
void save_config(AppContext *app) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        DEBUG_PRINT("[ DEBUG ]: Error opening config file for writing\n");
        return;
    }

    fprintf(fp, "# part of aiterm project\n");
    fprintf(fp, "# aiterm.conf version: %s\n", CONFIG_FILE_VERSION);
    fprintf(fp, "# Configuration file for aiterm\n");
    fprintf(fp, "# WARNING: Any changes made to this file will be overwritten\n\n");
    fprintf(fp, "color=%d\n", app->sys.debug_color);
    fprintf(fp, "provider=%s\n", app->provider_config.provider ? app->provider_config.provider : "gemini");
    fprintf(fp, "model=%s\n", app->aiterm_runtime.model ? app->aiterm_runtime.model : "gemini-flash-latest");
    fprintf(fp, "provider_base_url=%s\n", app->provider_config.base_url ? app->provider_config.base_url : "");
    fprintf(fp, "provider_endpoint=%s\n", app->provider_config.endpoint ? app->provider_config.endpoint : "");
    fprintf(fp, "provider_auth_header=%s\n", app->provider_config.auth_header ? app->provider_config.auth_header : "");
    fprintf(fp, "provider_auth_scheme=%s\n", app->provider_config.auth_scheme ? app->provider_config.auth_scheme : "");
    fprintf(fp, "provider_query_key=%s\n", app->provider_config.query_key_name ? app->provider_config.query_key_name : "");
    fprintf(fp, "provider_api_key_in_query=%d\n", app->provider_config.api_key_in_query);
    char *encrypted_api_key = crypt_to_hex(app->security.api_key ? app->security.api_key : "", app->security.master_key);
    if (encrypted_api_key) {
	fprintf(fp, "api_key=%s\n", encrypted_api_key);
	free(encrypted_api_key);
    } else {
	fprintf(fp, "api_key=\n");
    }
    //fprintf(fp, "api_key=%s\n", app->security.api_key ? app->security.api_key : "");
    fprintf(fp, "db_host=%s\n", app->database.db_host ? app->database.db_host : "localhost");
    fprintf(fp, "db_user=%s\n", app->database.db_user ? app->database.db_user : "root");

    // NEW: Encrypt the in-memory plaintext password before writing to file
    char *encrypted_pass = crypt_to_hex(app->database.db_pass ? app->database.db_pass : "", app->security.master_key);
    if (encrypted_pass) {
        fprintf(fp, "db_pass=%s\n", encrypted_pass);
        free(encrypted_pass);
    } else {
        fprintf(fp, "db_pass=\n");
    }

    fprintf(fp, "db_name=%s\n", app->database.db_name ? app->database.db_name : "aiterm_db");
    fprintf(fp, "term_transparency=%f\n", app->gui.transparency);
    fprintf(fp, "ai_transparency=%f\n", app->gui.ai_transparency);
    fprintf(fp, "terminal_font=%s\n", app->gui.terminal_font);
    fprintf(fp, "ai_font=%s\n", app->gui.ai_font);
    fprintf(fp, "tee_enabled=%d\n", app->sys.tee_enabled);
    fprintf(fp, "autoreply_enabled=%d\n", app->sys.autoreply_enabled);
    fprintf(fp, "auto_execute_enabled=%d\n", app->sys.auto_execute_enabled);
    fprintf(fp, "ratelimit_enabled=%d\n", app->sys.ratelimit_enabled);
    fprintf(fp, "rpm=%d\n", app->limiter.requests_per_minute);
    fprintf(fp, "smart_cache_enabled=%d\n", app->sys.smart_cache_enabled);
    fprintf(fp, "write_to_global=%d\n", app->session.write_to_global);
    fprintf(fp, "read_from_global=%d\n", app->session.read_from_global);
    fprintf(fp, "noise_filter_enabled=%d\n", app->sys.noise_filter_enabled);
    fprintf(fp, "debug_mode=%d\n", app->sys.debug_mode);
    fprintf(fp, "send_snmp_payload=%d\n", app->SnmpContext.enable_gemini_feed);
    fprintf(fp, "snmp_ticker_enabled=%d\n", app->sys.snmp_ticker_enabled);
    fprintf(fp, "snmp_poll_interval=%d\n",app->SnmpContext.poll_interval_sec);
    fprintf(fp, "xml_tagging=%d\n", app->xml.tagging_enabled);
    fprintf(fp, "ai_retry_enabled=%d\n", app->retry_config.is_enabled);
    fprintf(fp, "ai_retry_max_retries=%d\n", app->retry_config.max_retries);
    fprintf(fp, "ai_retry_delay_sec=%d\n", app->retry_config.delay_sec);
    fprintf(fp, "load_from_session=%d\n", app->sys.load_from_session);
    fprintf(fp, "idle_timeout_minutes=%u\n", idle_get_timeout_minutes(app));
    fprintf(fp, "# End of Config file.\n\n");

    fclose(fp);
    DEBUG_PRINT("[ DEBUG ]: Settings saved to aiterm.conf\n");
}

void load_config(AppContext *app) {
    app->security.api_key = NULL;
    app->provider_config.provider = strdup("openai");

    app->gui.terminal_font = strdup("Monospace 10");
    app->gui.ai_font = strdup("Monospace 10");

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        DEBUG_PRINT("[ DEBUG ]: Config file %s not found\n", CONFIG_FILE);
        return;
    }
    DEBUG_PRINT("%s config file: %s\n", LOADED_PREFIX(app), CONFIG_FILE);

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
	line[strcspn(line, "\r\n")] = 0;
	if (*line == '#') {
                // Do nothing this line starts with #
                DEBUG_PRINT("%s Skipping commented line %s\n", SKIP_VAL(app), app->ansi.normal);
        } else if (strstr(line, "color=")) {
		app->sys.debug_color = atoi(strchr(line, '=') + 1);
		init_colors(app);
		char *color_val = app->sys.debug_color ? ON_VAL(app) : OFF_VAL(app);
		DEBUG_PRINT("%s  debug color enabled: [%s]%s\n", LOADED_PREFIX(app),
			color_val, app->ansi.normal);
	} else if (strstr(line, "api_key=")) {
		char *val = strchr(line, '=') + 1;
		if (app->security.api_key) free(app->security.api_key);
		app->security.api_key = hex_to_decrypt(val, app->security.master_key);
		DEBUG_PRINT("%s API Key%s\n", DECRYPTED_PREFIX(app), app->ansi.normal);
	} else if (strstr(line, "provider=")) {
		char *val = strchr(line, '=') + 1;
		if (app->provider_config.provider) free(app->provider_config.provider);
		app->provider_config.provider = strdup(val);
		DEBUG_PRINT("%s  Provider: [%s%s%s]%s\n", LOADED_PREFIX(app),
			app->ansi.cyan, app->provider_config.provider, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "model=")) {
		char *val = strchr(line, '=') + 1;
		if (app->aiterm_runtime.model) {
			free(app->aiterm_runtime.model);
		}
		app->aiterm_runtime.model = strdup(val);
		DEBUG_PRINT("%s  Model: [%s%s%s]%s\n", LOADED_PREFIX(app),
                        app->ansi.cyan, app->aiterm_runtime.model, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "provider_base_url=")) {
        char *val = strchr(line, '=') + 1;
        g_free(app->provider_config.base_url);
        app->provider_config.base_url = g_strdup(val);
    } else if (strstr(line, "provider_endpoint=")) {
        char *val = strchr(line, '=') + 1;
        g_free(app->provider_config.endpoint);
        app->provider_config.endpoint = g_strdup(val);
    } else if (strstr(line, "provider_auth_header=")) {
        char *val = strchr(line, '=') + 1;
        g_free(app->provider_config.auth_header);
        app->provider_config.auth_header = g_strdup(val);
    } else if (strstr(line, "provider_auth_scheme=")) {
        char *val = strchr(line, '=') + 1;
        g_free(app->provider_config.auth_scheme);
        app->provider_config.auth_scheme = g_strdup(val);
    } else if (strstr(line, "provider_query_key=")) {
        char *val = strchr(line, '=') + 1;
        g_free(app->provider_config.query_key_name);
        app->provider_config.query_key_name = g_strdup(val);
    } else if (strstr(line, "provider_api_key_in_query=")) {
        app->provider_config.api_key_in_query = atoi(strchr(line, '=') + 1) != 0;
	} else if (strstr(line, "db_host=")) {
		char *val = strchr(line, '=') + 1;
		if (app->database.db_host) {
			free(app->database.db_host);
		}
		app->database.db_host = strdup(val);
		DEBUG_PRINT("%s  DB Host: [%s%s%s]%s\n",  LOADED_PREFIX(app),
                        app->ansi.cyan, app->database.db_host, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "db_user=")) {
		char *val = strchr(line, '=') + 1;
		if (app->database.db_user) {
			free(app->database.db_user);
		}
		app->database.db_user = strdup(val);
		DEBUG_PRINT("%s  DB User: [%s%s%s]%s\n",  LOADED_PREFIX(app),
                        app->ansi.cyan, app->database.db_user, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "db_pass=")) {
		char *val = strchr(line, '=') + 1;
		if (app->database.db_pass) {
			free(app->database.db_pass);
		}
		app->database.db_pass = hex_to_decrypt(val, app->security.master_key);
		DEBUG_PRINT("%s DB Password: [%sxxxxxx%s]%s\n", DECRYPTED_PREFIX(app), 
			app->ansi.yellow, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "db_name")) {
		char *val = strchr(line, '=') + 1;
		if (app->database.db_name) {
			free(app->database.db_name);
		}
		app->database.db_name = strdup(val);
		DEBUG_PRINT("%s  DB Name: [%s%s%s]%s\n",  LOADED_PREFIX(app),
                        app->ansi.cyan, app->database.db_name, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "ai_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->gui.ai_transparency = atof(val);
		if (app->gui.ai_transparency < 0.1) app->gui.ai_transparency = 0.8;
		DEBUG_PRINT("%s  AI transparency: [%s%f%s]%s\n",  LOADED_PREFIX(app),
                        app->ansi.cyan, app->gui.ai_transparency, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "term_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->gui.transparency = atof(val);
		if (app->gui.transparency < 0.1) app->gui.transparency = 0.8;
		DEBUG_PRINT("%s  terminal transparency: [%s%f%s]%s\n",  LOADED_PREFIX(app),
                        app->ansi.cyan, app->gui.transparency, app->ansi.yellow, app->ansi.normal);
        } else if (strstr(line, "terminal_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->gui.terminal_font) {
			free(app->gui.terminal_font);
		}
		app->gui.terminal_font = strdup(val);
		DEBUG_PRINT("%s  terminal font: [%s%s%s]%s\n", LOADED_PREFIX(app),
			app->ansi.cyan, app->gui.terminal_font, app->ansi.yellow, app->ansi.normal);
	} else if (strstr(line, "ai_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->gui.ai_font) {
			free(app->gui.ai_font);
		}
		app->gui.ai_font = strdup(val);
                char *ai_font_val = g_malloc(128);
                snprintf(ai_font_val, 128, "%s%s%s", app->ansi.cyan, app->gui.ai_font, app->ansi.yellow);
		DEBUG_PRINT("%s  AI font: [%s] %s\n", LOADED_PREFIX(app),
			ai_font_val, app->ansi.normal);
	} else if (strstr(line, "tee_enabled=")) {
		app->sys.tee_enabled = atoi(strchr(line, '=') + 1);
		char *tee_val = app->sys.tee_enabled ? ON_VAL(app) : OFF_VAL(app);
		DEBUG_PRINT("%s  default tee enabled: [%s]%s\n", LOADED_PREFIX(app),
			tee_val, app->ansi.normal);
	} else if (strstr(line, "autoreply_enabled=")) {
		app->sys.autoreply_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_val = app->sys.autoreply_enabled ? ON_VAL(app) : OFF_VAL(app);
		DEBUG_PRINT("%s  default auto reply enabled: [%s]%s\n", LOADED_PREFIX(app),
			auto_val, app->ansi.normal);
	} else if (strstr(line, "auto_execute_enabled=")) {
		app->sys.auto_execute_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_exec_val = app->sys.auto_execute_enabled ? ON_VAL(app) : OFF_VAL(app);
		DEBUG_PRINT("%s  Default auto execute enabled: [%s]%s\n", LOADED_PREFIX(app),
			auto_exec_val, app->ansi.normal);
	} else if (strstr(line, "ratelimit_enabled=")) {
        	app->sys.ratelimit_enabled = atoi(strchr(line, '=') + 1);
                const char *ratelimit_enabled_val = app->sys.ratelimit_enabled ? ON_VAL(app) : OFF_VAL(app);
        	DEBUG_PRINT("%s  Rate limit enabled: [%s]%s\n", LOADED_PREFIX(app),
			ratelimit_enabled_val, app->ansi.normal);
        } else if (strstr(line, "send_snmp_payload=")) {
                app->SnmpContext.enable_gemini_feed  = atoi(strchr(line, '=') + 1);
                const char *SnmpContext_enable_gemini_feed_val = app->SnmpContext.enable_gemini_feed  ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  Send SNMP Payload: [%s]%s\n", LOADED_PREFIX(app),
			SnmpContext_enable_gemini_feed_val, app->ansi.normal);
        } else if (strstr(line, "snmp_ticker_enabled=")) {
                app->sys.snmp_ticker_enabled = atoi(strchr(line, '=') + 1);
                const char *snmp_ticker_enabled_val = app->sys.snmp_ticker_enabled ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  SNMP Ticker Enabled: [%s]%s\n", LOADED_PREFIX(app), 
			snmp_ticker_enabled_val, app->ansi.normal);
        } else if (strstr(line, "snmp_poll_interval=")) {
                app->SnmpContext.poll_interval_sec = atoi(strchr(line, '=') + 1);
                char *poll_interval_val = g_malloc(32);
                snprintf(poll_interval_val, 32, "%s%d%s", app->ansi.cyan, app->SnmpContext.poll_interval_sec, app->ansi.yellow); 
                DEBUG_PRINT("%s  SNMP Poll Interval: [%s]%s\n", LOADED_PREFIX(app), 
			poll_interval_val, app->ansi.normal);
        } else if (strstr(line, "rpm=")) {
                app->limiter.requests_per_minute = atoi(strchr(line, '=') + 1);
                DEBUG_PRINT("%s  Requests Per Minute (RPM): [%s%d%s]%s\n", LOADED_PREFIX(app),
			app->ansi.cyan, app->limiter.requests_per_minute, app->ansi.yellow, app->ansi.normal);
        } else if (strstr(line, "smart_cache_enabled=")) {
                app->sys.smart_cache_enabled = atoi(strchr(line, '=') + 1);
                const char *smart_cache_val = app->sys.smart_cache_enabled ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  Smart Cache enabled: [%s]%s\n", LOADED_PREFIX(app), 
			smart_cache_val, app->ansi.normal);
        } else if (strstr(line, "write_to_global=")) {
                app->session.write_to_global = atoi(strchr(line, '=') + 1);
                app->session.cfg_loaded_write_to_global = TRUE;
                const char *write_to_global_val = app->session.write_to_global ? "GLOBAL session" : "STRICT session";
                DEBUG_PRINT("%s  Write to database [%s%s%s]%s\n", LOADED_PREFIX(app),
			app->ansi.cyan,  write_to_global_val, app->ansi.yellow, app->ansi.normal);
        } else if (strstr(line, "read_from_global=")) {
                app->session.read_from_global = atoi(strchr(line, '=') + 1);
                app->session.cfg_loaded_read_from_global = TRUE;
                const char *read_from_global_val = app->session.read_from_global ? "GLOBAL session" : "STRICT session";
                DEBUG_PRINT("%s  Read from database [%s%s%s]%s\n", LOADED_PREFIX(app),
			app->ansi.cyan,  read_from_global_val, app->ansi.yellow, app->ansi.normal);
        } else if (strstr(line, "noise_filter_enabled=")) {
                app->sys.noise_filter_enabled = atoi(strchr(line, '=') + 1);
                const char *noise_filter_enabled_val = app->sys.noise_filter_enabled ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  Noise Filter Enabled [%s]%s\n", LOADED_PREFIX(app),
			noise_filter_enabled_val, app->ansi.normal);
        } else if (strstr(line, "debug_mode=")) {
                if (app->sys.debug_mode_override) {
                    DEBUG_PRINT("%s Loading debug mode override from command line%s\n", OVERRIDE_PREFIX(app), app->ansi.normal);
                } else {
                    app->sys.debug_mode = atoi(strchr(line, '=') + 1);
                    const char *debug_mode_val = app->sys.debug_mode ? ON_VAL(app) : OFF_VAL(app);
                    DEBUG_PRINT("%s  Debug Mode Enabled [%s]%s\n", LOADED_PREFIX(app),
			debug_mode_val, app->ansi.normal);
                }
        } else if (strstr(line, "xml_tagging=")) {
                app->xml.tagging_enabled = atoi(strchr(line, '=') + 1);
                const char *xml_tagging_enabled_val = app->xml.tagging_enabled ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  XML Payload Tagging Enabled [%s]%s\n", LOADED_PREFIX(app),
			xml_tagging_enabled_val, app->ansi.normal);
        } else if (strstr(line, "ai_retry_enabled=")) {
                app->retry_config.is_enabled = atoi(strchr(line, '=') + 1);
                app->retry_state.config.is_enabled = app->retry_config.is_enabled;
                const char *retry_is_enabled_val = app->retry_state.config.is_enabled ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  AI Retry Enabled [%s]%s\n", LOADED_PREFIX(app), 
			retry_is_enabled_val, app->ansi.normal);
        } else if (strstr(line, "ai_retry_max_retries=")) {
                app->retry_config.max_retries = atoi(strchr(line, '=') + 1);
                app->retry_state.config.max_retries = app->retry_config.max_retries;
		char *MAX_RETRY_VAL = g_malloc(64);
                snprintf(MAX_RETRY_VAL, 32, "%s%d%s", app->ansi.cyan, 
			app->retry_config.max_retries, app->ansi.yellow);
                DEBUG_PRINT("%s  AI Retry Max Attempts [%s] %s\n", LOADED_PREFIX(app), 
			MAX_RETRY_VAL, app->ansi.normal);
        } else if (strstr(line, "ai_retry_delay_sec=")) {
                app->retry_config.delay_sec = atoi(strchr(line, '=') + 1);
                app->retry_state.config.delay_sec = app->retry_config.delay_sec;
                DEBUG_PRINT("%s  AI Retry Delay [%d sec]%s\n", LOADED_PREFIX(app), 
			app->retry_config.delay_sec, app->ansi.normal);
        } else if (strstr(line, "load_from_session=")) {
                app->sys.load_from_session = atoi(strchr(line, '=') + 1);
                const char *load_from_session_val = app->sys.load_from_session ? ON_VAL(app) : OFF_VAL(app);
                DEBUG_PRINT("%s  Session based config enabled: [%s]%s\n", LOADED_PREFIX(app), load_from_session_val, app->ansi.normal);
        } else if (strstr(line, "idle_timeout_minutes=")) {
                long idle_minutes = strtol(strchr(line, '=') + 1, NULL, 10);
                if (idle_minutes >= 0 && idle_minutes <= 1440) {
                    app->idle.timeout_minutes = (guint)idle_minutes;
                    DEBUG_PRINT("%s  Idle timeout: [%u minutes]%s\n",
                        LOADED_PREFIX(app), app->idle.timeout_minutes, app->ansi.normal);
                }
        }
    }
    fclose(fp);
    sync_toggle_ui_elements(app);
}
