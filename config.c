// part of aiterm project
// config.c
// Various utilities used in this project
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

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

static const char *CONFIG_FILE_VERSION="1.2";
// Function to save config file
void save_config(AppContext *app) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        DEBUG_PRINT("[DEBUG]: Error opening aiterm.conf for writing\n");
        return;
    }

    fprintf(fp, "# part of aiterm project\n");
    fprintf(fp, "# aiterm.conf version: %s\n", CONFIG_FILE_VERSION);
    fprintf(fp, "# Configuration file for aiterm\n");
    fprintf(fp, "# WARNING: Any changes made to this file will be overwritten\n\n");
    fprintf(fp, "provider=%s\n", app->provider ? app->provider : "gemini");
    fprintf(fp, "model=%s\n", app->model ? app->model : "gemini-flash-latest");
    char *encrypted_api_key = crypt_to_hex(app->api_key ? app->api_key : "", app->master_key);
    if (encrypted_api_key) {
	fprintf(fp, "api_key=%s\n", encrypted_api_key);
	free(encrypted_api_key);
    } else {
	fprintf(fp, "api_key=\n");
    }
    //fprintf(fp, "api_key=%s\n", app->api_key ? app->api_key : "");
    fprintf(fp, "db_host=%s\n", app->db_host ? app->db_host : "localhost");
    fprintf(fp, "db_user=%s\n", app->db_user ? app->db_user : "root");

    // NEW: Encrypt the in-memory plaintext password before writing to file
    char *encrypted_pass = crypt_to_hex(app->db_pass ? app->db_pass : "", app->master_key);
    if (encrypted_pass) {
        fprintf(fp, "db_pass=%s\n", encrypted_pass);
        free(encrypted_pass);
    } else {
        fprintf(fp, "db_pass=\n");
    }

    fprintf(fp, "db_name=%s\n", app->db_name ? app->db_name : "aiterm_db");
    fprintf(fp, "term_transparency=%f\n", app->transparency);
    fprintf(fp, "ai_transparency=%f\n", app->ai_transparency);
    fprintf(fp, "terminal_font=%s\n", app->terminal_font);
    fprintf(fp, "ai_font=%s\n", app->ai_font);
    fprintf(fp, "tee_enabled=%d\n", app->tee_enabled);
    fprintf(fp, "autoreply_enabled=%d\n", app->autoreply_enabled);
    fprintf(fp, "auto_execute_enabled=%d\n", app->auto_execute_enabled);
    fprintf(fp, "ratelimit_enabled=%d\n", app->ratelimit_enabled);
    fprintf(fp, "rpm=%d\n", app->limiter.requests_per_minute);
    fprintf(fp, "smart_cache_enabled=%d\n", app->smart_cache_enabled);
    fprintf(fp, "write_to_global=%d\n", app->session.write_to_global);
    fprintf(fp, "read_from_global=%d\n", app->session.read_from_global);
    fprintf(fp, "noise_filter_enabled=%d\n", app->noise_filter_enabled);
    fprintf(fp, "# End of Config file.\n\n");

    fclose(fp);
    DEBUG_PRINT("[DEBUG]: Settings saved to aiterm.conf\n");
}

void load_config(AppContext *app) {
    app->api_key = NULL;
    app->provider = strdup("openai");

    app->terminal_font = strdup("Monospace 10");
    app->ai_font = strdup("Monospace 10");

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        DEBUG_PRINT("[DEBUG]: Config file %s not found\n", CONFIG_FILE);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
	line[strcspn(line, "\r\n")] = 0;
	if (*line == '#') {
                // Do nothing this line starts with #
                DEBUG_PRINT("[DEBUG]: [SKIP] Skipping commented line\n");
	} else if (strstr(line, "api_key=")) {
		char *val = strchr(line, '=') + 1;
		if (app->api_key) free(app->api_key);
		app->api_key = hex_to_decrypt(val, app->master_key);
		DEBUG_PRINT("[DEBUG]: [LOADED] API Key\n");
	} else if (strstr(line, "provider=")) {
		char *val = strchr(line, '=') + 1;
		if (app->provider) free(app->provider);
		app->provider = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] Provider: [%s]\n", app->provider);
	} else if (strstr(line, "model=")) {
		char *val = strchr(line, '=') + 1;
		if (app->model) free(app->model);
		app->model = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] Model: [%s]\n", app->model);
	} else if (strstr(line, "db_host=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_host) free(app->db_host);
		app->db_host = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] DB Host: [%s]\n", app->db_host);
	} else if (strstr(line, "db_user=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_user) free(app->db_user);
		app->db_user = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] DB User: [%s]\n", app->db_user);
	} else if (strstr(line, "db_pass=")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_pass) free(app->db_pass);
		app->db_pass = hex_to_decrypt(val, app->master_key);
		DEBUG_PRINT("[DEBUG]: [LOADED] DB Password: [xxxxxx]\n");
	} else if (strstr(line, "db_name")) {
		char *val = strchr(line, '=') + 1;
		if (app->db_name) free(app->db_name);
		app->db_name = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] DB Name: [%s]\n", app->db_name);
	} else if (strstr(line, "ai_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->ai_transparency = atof(val);
		if (app->ai_transparency < 0.1) app->ai_transparency = 0.8;
		DEBUG_PRINT("[DEBUG]: [LOADED] AI transparency: [%f]\n", app->ai_transparency);
	} else if (strstr(line, "term_transparency=")) {
		char *val = strchr(line, '=') + 1;
		app->transparency = atof(val);
		if (app->transparency < 0.1) app->transparency = 0.8;
		DEBUG_PRINT("[DEBUG]: [LOADED] terminal transparency: [%f]\n", app->transparency);
        } else if (strstr(line, "terminal_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->terminal_font) free(app->terminal_font);
		app->terminal_font = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] terminal font: [%s]\n", app->terminal_font);
	} else if (strstr(line, "ai_font=")) {
		char *val = strchr(line, '=') + 1;
		if (app->ai_font) free(app->ai_font);
		app->ai_font = strdup(val);
		DEBUG_PRINT("[DEBUG]: [LOADED] AI font: [%s]\n", app->ai_font);
	} else if (strstr(line, "tee_enabled=")) {
		app->tee_enabled = atoi(strchr(line, '=') + 1);
		const char *tee_val = app->tee_enabled ? "ON" : "OFF";
		DEBUG_PRINT("[DEBUG]: [LOADED] default tee enabled: [%s]\n", tee_val);
	} else if (strstr(line, "autoreply_enabled=")) {
		app->autoreply_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_val = app->autoreply_enabled ? "ON" : "OFF";
		DEBUG_PRINT("[DEBUG]: [LOADED] default auto reply enabled: [%s]\n", auto_val);
	} else if (strstr(line, "auto_execute_enabled=")) {
		app->auto_execute_enabled = atoi(strchr(line, '=') + 1);
		const char *auto_exec_val = app->auto_execute_enabled ? "ON" : "OFF";
		DEBUG_PRINT("[DEBUG]: [LOADED] Default auto execute enabled: [%s]\n", auto_exec_val);
	} else if (strstr(line, "ratelimit_enabled=")) {
        	app->ratelimit_enabled = atoi(strchr(line, '=') + 1);
                const char *ratelimit_enabled_val = app->ratelimit_enabled ? "ON" : "OFF";
        	DEBUG_PRINT("[DEBUG]: [LOADED] Rate limit enabled: [%s]\n", ratelimit_enabled_val);
        } else if (strstr(line, "rpm=")) {
                app->limiter.requests_per_minute = atoi(strchr(line, '=') + 1);
                DEBUG_PRINT("[DEBUG]: [LOADED] Requests Per Minute (RPM): [%d]\n", app->limiter.requests_per_minute);
        } else if (strstr(line, "smart_cache_enabled=")) {
                app->smart_cache_enabled = atoi(strchr(line, '=') + 1);
                const char *smart_cache_val = app->smart_cache_enabled ? "ON" : "OFF";
                DEBUG_PRINT("[DEBUG]: [LOADED] Smart Cache enabled: [%s]\n", smart_cache_val);
        } else if (strstr(line, "write_to_global=")) {
                app->session.write_to_global = atoi(strchr(line, '=') + 1);
                app->session.cfg_loaded_write_to_global = TRUE;
                const char *write_to_global_val = app->session.write_to_global ? "GLOBAL session" : "STRICT session";
                DEBUG_PRINT("[DEBUG]: [LOADED] Write to database [%s]\n", write_to_global_val);
        } else if (strstr(line, "read_from_global=")) {
                app->session.read_from_global = atoi(strchr(line, '=') + 1);
                app->session.cfg_loaded_read_from_global = TRUE;
                const char *read_from_global_val = app->session.read_from_global ? "GLOBAL session" : "STRICT session";
                DEBUG_PRINT("[DEBUG]: [LOADED] Read from database [%s]\n", read_from_global_val);
        } else if (strstr(line, "noise_filter_enabled=")) {
                app->noise_filter_enabled = atoi(strchr(line, '=') + 1);
                const char *noise_filter_enabled_val = app->noise_filter_enabled ? "ON" : "OFF";
                DEBUG_PRINT("[DEBUG]: [LOADED] Noise Filter Enabled [%s]\n", noise_filter_enabled_val);
        }


    }
    fclose(fp);
}
