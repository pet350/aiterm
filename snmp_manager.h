// part of the aiterm project
// snmp_manager.h
// Header file for snmp functions
// By: Peter Talbott
// Assisted by: Gemini 
// aiterm The terminal emulator with an AI Pane
// August 2026

#ifndef SNMP_MANAGER_H
#define SNMP_MANAGER_H

#include "gui.h"

// Function prototypes

void init_snmp_subsystem(AppContext *app);
void dump_raw_snmp_payload_to_ai(AppContext *app);
void *snmp_poller_worker(void *data);
void snmp_start_poller(AppContext *app);
void snmp_stop_poller(AppContext *app);
void snmp_poll_all_targets(AppContext *app);
void snmp_force_poll(AppContext *app);

char *snmp_format_telemetry_payload_old(AppContext *app);
char *snmp_format_telemetry_payload(AppContext *app);

gboolean snmp_load_targets_from_db(AppContext *app);
gboolean update_snmp_ticker_payload_wrapper(gpointer data);
gboolean append_raw_payload_idle(gpointer data);
gboolean update_ticker_idle_cb(gpointer data);
gboolean update_ticker_polling_cb(gpointer data);

#endif

