// part of aiterm project
// update.c
// Functions for updateing AI
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include "update.h"
#include "openai.h"
#include "gemini.h"
#include "help.h"
#include "utils.h"
#include "tee_handler.h"
#include "policy_dao.h"

// Helper to map risk strings to integers for the DB schema
static int risk_str_to_int(const char *str) {
    if (!str) return 1;
    if (strcasecmp(str, "LOW") == 0) return 1;
    if (strcasecmp(str, "MEDIUM") == 0) return 2;
    if (strcasecmp(str, "HIGH") == 0) return 3;
    if (strcasecmp(str, "CRITICAL") == 0) return 4;
    return atoi(str); // Fallback for literal digit inputs
}

// Helper to translate internal DB integers back to user-friendly strings
static const char* risk_int_to_str(int risk) {
    switch(risk) {
        case 1:  return "LOW";
        case 2:  return "MEDIUM";
        case 3:  return "HIGH";
        case 4:  return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// Unified writing function for the AI Pane (Exactly 5 arguments)
void write_to_ai_pane(AppContext *app, const char *header, const char *body, const char *header_tag, const char *body_tag) {
    if (!app || !app->gemini_view) return;
    if (header) {
        append_ai_text(app, header, header_tag);
    }
    if (body) {
        append_ai_text(app, body, body_tag);
    }
    append_ai_text(app, "\n\n", NULL);
}

// Logic for process_for_ai (Tee Data Collection)
void process_for_ai(AppContext *app, const char *text, gboolean is_input) {
    if (!app->tee_enabled || !text) return;
    // app->sequence_id++;
    if (is_input) {
        tee_handle_input(app, text);
    } else {
        tee_handle_output(app, text); 
    }
}

// GUI update callback for AI responses
gboolean update_gui_with_response(gpointer data) {
    AIResponseData *rd = (AIResponseData *)data;
    if (!rd) return FALSE;

    if (rd->response_text) {
        if (strncmp(rd->response_text, "API ERROR:", 10) == 0) {
            // Display errors in Debian Red
            append_ai_text(rd->app, "System: ", "system_tag");
            append_ai_text(rd->app, rd->response_text, "body_tag");
        }  else if (rd->app->auto_execute_enabled && is_ai_command(rd->response_text)) {
            char *cmd = extract_ai_command(rd->response_text);
            char *cmd_name = extract_cmd_name(cmd);

            feed_terminal_header(VTE_TERMINAL(rd->app->terminal_view), cmd);
            // 2. REUSE POLICY GATE
            PolicyRecord *p = get_policy_for_command(rd->app, cmd_name);
            if (p && strcmp(p->type, "BLOCK") == 0) {
                write_to_ai_pane(rd->app, "System: ", "AI attempted a BLOCKED command.", "system_tag", "body_tag");
            } else {
                // Execute safely
                vte_terminal_feed(VTE_TERMINAL(rd->app->terminal_view), cmd, -1);
                vte_terminal_feed(VTE_TERMINAL(rd->app->terminal_view), "\n", 1);
                write_to_ai_pane(rd->app, "System: ", "AI command executed.", "cmd_tag", "cmd_tag");
            }
            if (p) free_policy_record(p);
            free(cmd);
            free(cmd_name);
        } else {
            // AI Response: Header in Green, Content in Yellow
            write_to_ai_pane(rd->app, "AI: ", rd->response_text, "ai_tag", "body_tag");
        }
    } else {
        write_to_ai_pane(rd->app, "System: ", "No response from provider.", "cmd_tag", "cmd_tag");
    }

    gtk_label_set_text(GTK_LABEL(rd->app->status_label), "Ready");

    rd->app->is_processing = FALSE;
    if (rd->response_text) free(rd->response_text);
    if (rd->original_prompt) free(rd->original_prompt);
    free(rd);
    return FALSE;
}

// Local command handler
static gboolean handle_local_command(const char *input, AppContext *app) {
    if (strcasecmp(input, "help") == 0) {
        write_to_ai_pane(app, "[ Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "features") == 0) {
        write_to_ai_pane(app, "[ Features ]\n", get_features_text(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "hw") == 0) {
        write_to_ai_pane(app, "[ System Hardware ]\n", get_hw_stats(), "cmd_tag", "ai_tag");
    } else if (strcasecmp(input, "status") == 0) {
	display_status(app);
    } else if (strcasecmp(input, "clear") == 0) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gemini_view));
        gtk_text_buffer_set_text(buf, "", -1);
    } else if (strcasecmp(input, "provider") == 0) {
        char info[512];
        snprintf(info, sizeof(info), "Provider: %s\nModel: %s", app->provider, app->model);
        write_to_ai_pane(app, "System: ", info, "cmd_tag", "cmd_tag");
    }
    // SEPARATE COMMAND: Tee Data Collection
    else if (strcasecmp(input, "tee on") == 0) {
        app->tee_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "Tee Collection ENABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "tee off") == 0) {
        app->tee_enabled = FALSE;
        // LOGIC: If Tee is off, Autoreply MUST be off
        app->autoreply_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "Tee Collection & Auto-Reply DISABLED", "cmd_tag", "cmd_tag");
    }
    // SEPARATE COMMAND: Auto Reply Logic
    else if (strcasecmp(input, "autoreply on") == 0) {
        app->autoreply_enabled = TRUE;
        // LOGIC: If Autoreply is on, Tee MUST be on
        app->tee_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "AI Auto-Reply & Tee Collection ENABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "autoreply off") == 0) {
        app->autoreply_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "AI Auto-Reply DISABLED", "cmd_tag", "cmd_tag");
    }
    // SEPARATE COMMAND: Auto-Execute Logic
    else if (strcasecmp(input, "auto execute on") == 0) {
        app->auto_execute_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "AI Auto-Execute ENABLED (Proceed with caution)", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "auto execute off") == 0) {
        app->auto_execute_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "AI Auto-Execute DISABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "auto on") == 0) {
        app->auto_execute_enabled = TRUE;
        app->autoreply_enabled = TRUE;
        app->tee_enabled = TRUE;
        write_to_ai_pane(app, "System: ", "Tee Collection:\t\tENABLED", "cmd_tag", "cmd_tag");
        write_to_ai_pane(app, "System: ", "AI Auto-Reply:\t\tENABLED", "cmd_tag", "cmd_tag");
        write_to_ai_pane(app, "System: ", "AI Auto-Execute:\tENABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "auto off") == 0) {
        app->auto_execute_enabled = FALSE;
        app->autoreply_enabled = FALSE;
        app->tee_enabled = FALSE;
        write_to_ai_pane(app, "System: ", "Tee Collection:\t\tDISABLED", "cmd_tag", "cmd_tag");
        write_to_ai_pane(app, "System: ", "AI Auto-Reply:\t\tDISABLED", "cmd_tag", "cmd_tag");
        write_to_ai_pane(app, "System: ", "AI Auto-Execute:\tDISABLED", "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "version") == 0) {
        write_to_ai_pane(app, "System: ", get_version_info(), "cmd_tag", "cmd_tag");
    } else if (strcasecmp(input, "history") == 0) {
        display_all_history(app);
    } else if (strcasecmp(input, "save config") == 0) {
	save_config(app);
	write_to_ai_pane(app, "System: ", "Saved current configuration", "system_tag", "ai_tag");
    } else if (strcasecmp(input, "load config") == 0) {
        load_config(app);
        write_to_ai_pane(app, "System: ", "Loaded settings from configuration file", "system_tag", "ai_tag");
    } else if (g_str_has_prefix(input, "policy")) {
        char func[32] = {0};
        char args_buf[256] = {0};

        // Parse out the function keyword.
        // Handles "/policy ADD ...", "/policy LIST", or just "/policy"
        int parsed_tokens = sscanf(input, "policy %31s %[^\n]", func, args_buf);

        if (parsed_tokens < 1) {
            write_to_ai_pane(app, "System: ", "Usage: policy <ADD|DELETE|LIST|FIND> [args]", "system_tag", "body_tag");
            return TRUE;
        }

        // Convert the function verb to uppercase for easy comparison
        for (int i = 0; func[i]; i++) func[i] = toupper((unsigned char)func[i]);

        // --- SUB-COMMAND: ADD ---
        if (strcmp(func, "ADD") == 0) {
            char cmd[128] = {0}, type[32] = {0}, risk_str[32] = {0};
            if (sscanf(args_buf, "%127s %31s %31s", cmd, type, risk_str) < 3) {
                write_to_ai_pane(app, "System: ", "Syntax: policy ADD <command|*> <ALLOW|BLOCK|APPROVE> <LOW|MEDIUM|HIGH|CRITICAL>", "system_tag", "body_tag");
                return TRUE;
            }

            // Normalize policy action text to uppercase
            for (int i = 0; type[i]; i++) type[i] = toupper((unsigned char)type[i]);

            PolicyRecord p;
            p.name = cmd;
            p.type = type;
            p.risk = risk_str_to_int(risk_str);

            if (set_command_policy(app, &p)) {
                char out[256];
                snprintf(out, sizeof(out), "Policy updated successfully for '%s'.", cmd);
                write_to_ai_pane(app, "System: ", out, "system_tag", "body_tag");
            } else {
                write_to_ai_pane(app, "System: ", "Failed to update database policy.", "system_tag", "body_tag");
            }
        }

        // --- SUB-COMMAND: DELETE ---
        else if (strcmp(func, "DELETE") == 0) {
            char cmd[128] = {0};
            if (sscanf(args_buf, "%127s", cmd) < 1) {
                write_to_ai_pane(app, "System: ", "Syntax: policy DELETE <command>", "system_tag", "body_tag");
                return TRUE;
            }

            if (delete_command_policy(app, cmd)) {
                char out[256];
                snprintf(out, sizeof(out), "Deleted policy rule for command '%s'.", cmd);
                write_to_ai_pane(app, "System: ", out, "system_tag", "body_tag");
            } else {
                write_to_ai_pane(app, "System: ", "Failed to delete policy from database.", "system_tag", "body_tag");
            }
        }

        // --- SUB-COMMAND: LIST ---
        else if (strcmp(func, "LIST") == 0) {
            GList *list = get_all_policies(app);
            if (!list) {
                write_to_ai_pane(app, "System: ", "No rules currently registered in the policy database.", "system_tag", "body_tag");
                return TRUE;
            }

            write_to_ai_pane(app, "[ Active Security Policy Table ]\n", "--------------------------------------------\n", "system_tag", "body_tag");
            for (GList *l = list; l != NULL; l = l->next) {
                PolicyRecord *p = (PolicyRecord*)l->data;
                char entry[256];
                snprintf(entry, sizeof(entry), "  Command: %-15s | Mode: %-8s | Risk: %s\n", 
                         p->name, p->type, risk_int_to_str(p->risk));
                append_ai_text(app, entry, "body_tag");
                free_policy_record(p);
            }
            g_list_free(list);
            append_ai_text(app, "\n", NULL);
        }

        // --- SUB-COMMAND: FIND ---
        else if (strcmp(func, "FIND") == 0) {
            char cmd[128] = {0};
            if (sscanf(args_buf, "%127s", cmd) < 1) {
                write_to_ai_pane(app, "System: ", "Syntax: policy FIND <command>", "system_tag", "body_tag");
                return TRUE;
            }

            PolicyRecord *p = get_policy_for_command(app, cmd);
            if (p) {
                char out[256];
                snprintf(out, sizeof(out), "Match Found -> Command: %s | Mode: %s | Risk Level: %s",
                         p->name, p->type, risk_int_to_str(p->risk));
                write_to_ai_pane(app, "System: ", out, "system_tag", "body_tag");
                free_policy_record(p);
            } else {
                write_to_ai_pane(app, "System: ", "No policy matches found for that criteria.", "system_tag", "body_tag");
            }
        }

        // --- FALLBACK: INVALID SUB-COMMAND ---
        else {
            write_to_ai_pane(app, "System: ", "Unknown sub-command. Options: ADD, DELETE, LIST, FIND", "system_tag", "body_tag");
        }

        return TRUE; // Local command handled successfully
    } else if (strcasecmp(input, "exit") == 0) {
        gtk_main_quit();
    } else {
        return FALSE; // Not a local command
    }
    return TRUE;      // Is a local command
}

void on_input_activate(GtkEntry *entry, gpointer data) {
    AppContext *app = (AppContext *)data;
    const char *input_text = gtk_entry_get_text(entry);
    if (strlen(input_text) == 0) return;

    // 1. Handle Local Commands (Do NOT block these)
    if (handle_local_command(input_text, app)) {
        gtk_entry_set_text(entry, "");
        return;
    }

    // 2. SECURITY GATE (The new block)
    // Note: extract_cmd_name is a helper function to isolate the base command
    // from potential arguments (e.g., "cat /etc/passwd" -> "cat")
    char *cmd_name = extract_cmd_name(input_text);
    PolicyRecord *p = get_policy_for_command(app, cmd_name);

    if (p) {
        if (strcmp(p->type, "BLOCK") == 0) {
            write_to_ai_pane(app, "System: ", "Command BLOCKED by security policy.", "system_tag", "body_tag");
            free_policy_record(p);
            free(cmd_name);
            gtk_entry_set_text(entry, "");
            return;
        }  else if (strcmp(p->type, "APPROVE") == 0) {
        	write_to_ai_pane(app, "System: ", "Manual approval required for this command.", "system_tag", "body_tag");
            // TRIGGER THE DIALOG
            request_human_approval(app, input_text);
            free_policy_record(p);
            free(cmd_name);
            gtk_entry_set_text(entry, "");
            return;
        } 
        // If "ALLOW", we fall through to the AI/terminal logic below
        free_policy_record(p);
    }
    free(cmd_name);
    
    // --- NEW: 0.7.5-beta:  Guard against double-processing ---
    if (app->is_processing) {
        update_status_label(app, "AI is already busy...");
        return;
    }

    // Rate Limiting
    static time_t last_request_time = 0;
    time_t now = time(NULL);
    if (now - last_request_time < 2) {
        write_to_ai_pane(app, "System: ", "Wait 2 seconds...", "cmd_tag", "cmd_tag");
        return;
    }
    last_request_time = now;

    gtk_label_set_text(GTK_LABEL(app->status_label), "AI is thinking...");
    write_to_ai_pane(app, "You: ", input_text, "user_tag", NULL);

    AIThreadData *td = g_malloc0(sizeof(AIThreadData));
    td->app = app;
    td->prompt = g_strdup(input_text);
    g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td);

    gtk_entry_set_text(entry, "");
}

void update_status_label(AppContext *app, const char *status) {
    if (app && app->status_label) {
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
    }
}

// New helper function to handle user interaction
void request_human_approval(AppContext *app, const char *input_text) {
    GtkWidget *dialog;
    gint result;

    dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Security Alert: Command Requires Approval\n\nCommand: %s\n\nProceed with execution?", 
                                    input_text);

    gtk_window_set_title(GTK_WINDOW(dialog), "Manual Approval Required");
    
    // Run the dialog and block execution until user clicks a button
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (result == GTK_RESPONSE_OK) {
        // User clicked OK - proceed to the AI execution logic
        write_to_ai_pane(app, "System: ", "Command Approved. Executing...", "system_tag", "body_tag");
        
        // --- Execute the AI worker logic here ---
        AIThreadData *td = g_malloc0(sizeof(AIThreadData));
        td->app = app;
        td->prompt = g_strdup(input_text);
        g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td);
    } else {
        write_to_ai_pane(app, "System: ", "Command Canceled by user.", "system_tag", "body_tag");
    }
}
