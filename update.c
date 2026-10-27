// part of aiterm project
// update.c
// Functions for updateing AI
// By: Peter Talbott
// Assisted by: Gemini
// April 2026 - August 2026

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "gui.h"
#include "update.h"
#include "openai.h"
#include "gemini.h"
#include "help.h"
#include "utils.h"
#include "tee_handler.h"
#include "policy_dao.h"
#include "commands.h"
#include "autoexec.h"
#include "terminal.h"

// Rate Limiting
static time_t last_request_time = 0;

// Function will carry out a command on the terminal after passing security check
void execute_ai_command(AppContext *app, const char *ai_text) {
    // SECURITY GATE (The new block)
    // Note: extract_cmd_name is a helper function to isolate the base command
    // from potential arguments (e.g., "cat /etc/passwd" -> "cat")
    char *cmd = extract_ai_command(ai_text); // Gets the full "command arg" string
    if (!cmd) return;

    char *cmd_name = extract_cmd_name(cmd);  // Gets just "command"
    gboolean allowed = FALSE;

    // 1. SECURITY GATE (Centralized)
    PolicyRecord *p = get_policy_for_command(app, cmd_name);
    if (p) {
        if (strcmp(p->type, "BLOCK") == 0) {
            write_to_ai_pane(app, "System: ", "AI command BLOCKED by policy.", "system_tag", "body_tag");
            free_policy_record(p);
            free(cmd); free(cmd_name);
            return;
        } else if (strcmp(p->type, "APPROVE") == 0) {
            write_to_ai_pane(app, "System: ", "AI command requires manual approval.", "system_tag", "body_tag");
            // You can re-use your request_human_approval function here
            if (request_human_approval(app, cmd)) {
		allowed = TRUE;
	    } else {
                free_policy_record(p);
                free(cmd); free(cmd_name);
                return;
	    }
        } else {
	    allowed = TRUE;
	}
        free_policy_record(p);
    }
    if (allowed) {
        // 2. EXECUTION (The "Allow" path)
        vte_terminal_feed(VTE_TERMINAL(app->gui.terminal_view), cmd, -1);
        vte_terminal_feed(VTE_TERMINAL(app->gui.terminal_view), "\n", 1);
        write_to_ai_pane(app, "System: ", "AI command executed.", "cmd_tag", "body_tag");
    }
    free(cmd);
    free(cmd_name);
}

// Helper to map risk strings to integers for the DB schema
int risk_str_to_int(const char *str) {
    if (!str) return 1;
    if (strcasecmp(str, "LOW") == 0) return 1;
    if (strcasecmp(str, "MEDIUM") == 0) return 2;
    if (strcasecmp(str, "HIGH") == 0) return 3;
    if (strcasecmp(str, "CRITICAL") == 0) return 4;
    return atoi(str); // Fallback for literal digit inputs
}

// Helper to translate internal DB integers back to user-friendly strings
const char* risk_int_to_str(int risk) {
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
    if (!app || !app->gui.gemini_view) return;
    if (header) {
        append_ai_text(app, header, header_tag);
    }
    if (body) {
        append_ai_text(app, body, body_tag);
    }
    append_ai_text(app, "\n\n", NULL);
    scroll_ai_pane_to_bottom(app);
}

// Logic for process_for_ai (Tee Data Collection)
void process_for_ai(AppContext *app, const char *text, gboolean is_input) {
    if (!app->sys.tee_enabled || !text) return;
    // app->database.sequence_id++;
    if (is_input) {
        tee_handle_input(app, text);
    } else {
        tee_handle_output(app, text);
    }
}

// GUI update callback for AI responses
// Total rework 0.9.6-gamma
gboolean update_gui_with_response(gpointer data) {
    AIResponseData *rd = (AIResponseData *)data;
    if (!rd) return FALSE;

    if (rd->response_text) {
        if (strncmp(rd->response_text, "API ERROR:", 10) == 0) {
            // Display errors in Debian Red
            append_ai_text(rd->app, "System: ", "system_tag");
            append_ai_text(rd->app, rd->response_text, "body_tag");
            scroll_ai_pane_to_bottom(global_app);
        } else {
            // 1. ALWAYS render the AI response text to the side pane first
            write_to_ai_pane(rd->app, "AI: ", rd->response_text, "ai_tag", "body_tag");

            /* Auto Execute is dispatched below, after any requested tab switch.
             * This ensures command payloads target the tab the AI selected rather
             * than the tab that happened to be active when the response arrived. */
        }
    } else {
        write_to_ai_pane(rd->app, "System: ", "No response from provider.", "cmd_tag", "cmd_tag");
    }

    /* Execute navigation only after rendering the response. All GTK access is
     * on this main-loop callback. Auto Execute is dispatched only after this
     * block, so a valid change_tab action changes the execution destination first. */
    gboolean navigation_succeeded = (rd->requested_tab_id == NULL);
    if (rd->requested_tab_id && rd->app->gui.notebook && GTK_IS_NOTEBOOK(rd->app->gui.notebook)) {
        gint page_count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(rd->app->gui.notebook));
        gint target_page = -1;
        for (gint page_index = 0; page_index < page_count && page_index < MAX_TABS; page_index++) {
            if (g_strcmp0(rd->app->tabs[page_index].tab_id, rd->requested_tab_id) == 0) {
                target_page = page_index;
                break;
            }
        }
        if (target_page >= 0) {
            navigation_succeeded = TRUE;
            gtk_notebook_set_current_page(GTK_NOTEBOOK(rd->app->gui.notebook), target_page);
            if (rd->app->tabs[target_page].vte && GTK_IS_WIDGET(rd->app->tabs[target_page].vte))
                gtk_widget_grab_focus(rd->app->tabs[target_page].vte);
            const char *title = (rd->app->tabs[target_page].label && GTK_IS_LABEL(rd->app->tabs[target_page].label))
                ? gtk_label_get_text(GTK_LABEL(rd->app->tabs[target_page].label)) : "Terminal";
            char *notice = g_strdup_printf("AI switched to terminal tab %d (%s).\n", target_page + 1, title);
            write_to_ai_pane(rd->app, "System: ", notice, "cmd_tag", "cmd_tag");
            g_free(notice);
        } else {
            write_to_ai_pane(rd->app, "System: ", "AI requested a tab that no longer exists; navigation was ignored.\n", "cmd_tag", "cmd_tag");
        }
    }

    /* Run command payloads only after navigation has completed. A response
     * requesting another inspection is an intermediate investigation step, so
     * its commands are deferred rather than executed. If navigation failed,
     * do not risk running the command in the wrong terminal. */
    if (rd->response_text &&
        strncmp(rd->response_text, "API ERROR:", 10) != 0 &&
        rd->app->sys.auto_execute_enabled &&
        navigation_succeeded &&
        !rd->requested_inspect_tab_id) {
        process_auto_execution(rd->app, rd->response_text);
    }

    /* A targeted inspection is a bounded follow-up request. Capture the
     * requested tab only on the GTK thread, append that evidence to the
     * original bounded snapshot, and continue the same investigation. */
    if (rd->requested_inspect_tab_id) {
        if (rd->investigation_step >= 2) {
            write_to_ai_pane(rd->app, "System: ",
                "Targeted inspection limit reached (2 per request); no further tab inspection was started.\n",
                "cmd_tag", "body_tag");
        } else {
            char *targeted_context = terminal_capture_tab_context_by_id(
                rd->app, rd->requested_inspect_tab_id);
            char *combined_context = g_strdup_printf(
                "%s\n\n--- TARGETED INSPECTION %u ---\n%s",
                rd->terminal_context ? rd->terminal_context : "Initial tab context unavailable.",
                rd->investigation_step + 1,
                targeted_context ? targeted_context : "Targeted tab context unavailable.");
            g_free(targeted_context);

            AIThreadData *followup = g_malloc0(sizeof(*followup));
            followup->app = rd->app;
            followup->original_prompt = g_strdup(rd->original_prompt ? rd->original_prompt : "Continue the current investigation.");
            followup->prompt = g_strdup_printf(
                "AITERM CROSS-TAB INVESTIGATION CONTINUATION\n"
                "Original user request: %s\n\n"
                "Your previous response/findings: %s\n\n"
                "New evidence from the specifically requested tab is included in terminal context. "
                "Continue the same investigation, keep each finding attributed to its source tab, "
                "and distinguish observed evidence from inference. Do not invent command output. "
                "You may request one more targeted tab inspection using the final-line marker "
                "[[AITERM_ACTION: inspect_tab TAB_ID]] only if it is necessary; the system allows "
                "at most two targeted inspections total for this user request. You may instead "
                "switch the visible tab with [[AITERM_ACTION: change_tab TAB_ID]]. Use at most "
                "one action marker, and only as the final non-whitespace content. Navigation and "
                "inspection markers do not authorize command execution.",
                rd->original_prompt ? rd->original_prompt : "(original request unavailable)",
                rd->response_text ? rd->response_text : "No previous findings were returned.");
            followup->terminal_context = combined_context;
            followup->investigation_step = rd->investigation_step + 1;

            char *target_notice = g_strdup_printf(
                "AI is inspecting tab context (%u of 2) and continuing the investigation...\n",
                followup->investigation_step);
            write_to_ai_pane(rd->app, "System: ", target_notice, "cmd_tag", "body_tag");
            g_free(target_notice);
            gtk_label_set_text(GTK_LABEL(rd->app->gui.status_label), "AI is investigating another tab...");
            g_thread_unref(g_thread_new("ai-investigate", (GThreadFunc)ai_thread_func, followup));

            /* The follow-up owns the combined context. Keep the busy flag set
             * until the final response (or bounded next inspection) completes. */
            g_free(rd->terminal_context);
            if (rd->response_text) free(rd->response_text);
            if (rd->original_prompt) free(rd->original_prompt);
            g_free(rd->requested_tab_id);
            g_free(rd->requested_inspect_tab_id);
            g_free(rd);
            return FALSE;
        }
    }

    gtk_label_set_text(GTK_LABEL(rd->app->gui.status_label), "Ready");

    g_atomic_int_set(&rd->app->sys.is_processing, 0);
    if (rd->response_text) free(rd->response_text);
    if (rd->original_prompt) free(rd->original_prompt);
    g_free(rd->requested_tab_id);
    g_free(rd->requested_inspect_tab_id);
    g_free(rd->terminal_context);
    g_free(rd);
    return FALSE;
}

// Local command handler
static gboolean handle_local_command(const char *input, AppContext *app) {
    if (strcasecmp(input, "legacy help") == 0) {
        write_to_ai_pane(app, "[ Extended Help System ]\n", get_help_text(), "cmd_tag", "cmd_tag");
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
	        scroll_ai_pane_to_bottom(app);
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

        if (app->local.cmd_history) {
            gboolean duplicate = FALSE;

            // Avoid cluttering history if you press Enter multiple times on the same command
            if (app->local.cmd_history->len > 0) {
                const char *last_cmd = g_ptr_array_index(app->local.cmd_history, app->local.cmd_history->len - 1);
                if (strcmp(last_cmd, input_text) == 0) {
                    duplicate = TRUE;
                }
            }

            // Append the unique command to your local array
            if (!duplicate) {
                g_ptr_array_add(app->local.cmd_history, g_strdup(input_text));
            }

            // Reset the tracking index cleanly to the bottom baseline
            app->local.history_index = app->local.cmd_history->len;
            g_free(app->local.history_temp_entry);
            app->local.history_temp_entry = NULL;
        }

    // --- NEW DYNAMIC DISPATCHER ---
    // If execute_command returns TRUE, it was a local command.
    // We clear the input and exit.
    if (execute_command(app, input_text)) {
        gtk_entry_set_text(entry, "");
        return;
    }

    // --- FALLBACK / LEGACY SUPPORT ---
    // If we reach here, it wasn't in the registry.
    // Check if it's one of the commands you haven't ported yet:
    // 1. Handle Local Commands (Do NOT block these)
    if (handle_local_command(input_text, app)) {
        gtk_entry_set_text(entry, "");
        return;
    }

    char *cleaned_text = strip_blank_lines(input_text);
    if (!cleaned_text || !*cleaned_text) {
        g_free(cleaned_text);
        return;
    }

    time_t now = time(NULL);
    if (now - last_request_time < 2) {
        write_to_ai_pane(app, "System: ", "Wait 2 seconds...", "cmd_tag", "cmd_tag");
        g_atomic_int_set(&app->sys.is_processing, 0);
        g_free(cleaned_text);
        return;
    }
    last_request_time = now;

    // Atomically reserve the single AI worker slot.
    if (!g_atomic_int_compare_and_exchange(&app->sys.is_processing, 0, 1)) {
        update_status_label(app, "AI is already busy...");
        g_free(cleaned_text);
        return;
    }

    /*
     * Build the AI terminal context in priority order:
     *
     *   1. Pending TEE accumulator, if output arrived since the last flush.
     *   2. Newest persisted TEE row, if the timed flush already saved it.
     *   3. VTE scrollback only as a last-resort fallback.
     *
     * Do NOT concatenate TEE with the VTE snapshot. The VTE snapshot can be
     * much larger and can contain the exact same terminal output plus older
     * scrollback. That was causing the provider to receive stale context even
     * though the fresh TEE capture had already been saved successfully.
     */
    char *terminal_context = NULL;
    const char *context_source = "none";

    char *lt_pl   = g_strdup(app->ansi.lt_purple);
    char *cy      = g_strdup(app->ansi.cyan);
    char *yl      = g_strdup(app->ansi.yellow);
    char *gr      = g_strdup(app->ansi.green);
    char *red     = g_strdup(app->ansi.red);
    char *nml     = g_strdup(app->ansi.normal);

    if (app->sys.tee_enabled || app->sys.autoreply_enabled) {
        terminal_capture_pending_tee(app);

        terminal_context = tee_peek_for_ai(app);
        if (terminal_context && *terminal_context) {
            context_source = "pending-tee";
        } else {
            g_free(terminal_context);
            terminal_context = tee_get_latest_for_ai(app);
            if (terminal_context && *terminal_context) {
                context_source = "latest-db-tee";
            } else {
                g_free(terminal_context);
                terminal_context = NULL;
            }
        }
    }

    if (!terminal_context) {
        /* GTK/VTE access remains on the UI thread. */
        terminal_context = terminal_capture_context(app);
        context_source = "vte-fallback";
    }

    /* Snapshot all tabs on the GTK thread. The AI receives bounded, labeled
     * context for background tabs as well as the normal focused-tab TEE. */
    char *all_tabs_context = terminal_capture_all_tabs_context(app);
    char *combined_context = g_strdup_printf(
        "%s\n\n--- FOCUSED TAB PRIORITY CONTEXT ---\n%s",
        all_tabs_context ? all_tabs_context : "AITERM multi-tab context unavailable.",
        terminal_context ? terminal_context : "No focused-tab context available.");
    g_free(all_tabs_context);
    g_free(terminal_context);
    terminal_context = combined_context;

    DEBUG_PRINT("[ %sDEBUG%s ]: [%sTEE CONTEXT%s] %ssource=[%s%s%s] bytes=[%s%zu%s]%s\n",
	lt_pl, nml, cy, nml, gr,
        red, context_source, gr,
        red, terminal_context ? strlen(terminal_context) : 0UL, gr, nml);

    /* Do not run command-policy/database work here for ordinary user input.
     * AI command execution is handled only after an AI response has produced
     * an explicit <cmd> block.  Keeping this path free of synchronous policy
     * lookups makes Enter/input processing remain a fast GTK operation. */

    gtk_label_set_text(GTK_LABEL(app->gui.status_label), "AI is thinking...");
    write_to_ai_pane(app, "You: ", cleaned_text, "user_tag", NULL);

    AIThreadData *td = g_malloc0(sizeof(AIThreadData));

    g_free(lt_pl);
    g_free(cy);
    g_free(yl);
    g_free(gr);
    g_free(red);
    g_free(nml);

    td->app = app;
    td->original_prompt = g_strdup(cleaned_text);
    td->prompt = g_strdup_printf(
        "AITERM TAB-AWARE REQUEST\n"
        "You may inspect the labeled multi-tab snapshot supplied as terminal context. "
        "Treat each tab as a distinct source and cite tab numbers/titles when useful. "
        "Each tab's initial snapshot is bounded. If a specific tab needs more evidence, "
        "request targeted inspection with exactly one final-line marker: "
        "[[AITERM_ACTION: inspect_tab TAB_ID]], using that tab's exact stable UUID. "
        "The system will retrieve a larger recent output snapshot and ask you to continue. "
        "At most two targeted inspections are allowed per user request. If switching the "
        "visible tab would help, use [[AITERM_ACTION: change_tab TAB_ID]] instead. Use at "
        "most one action marker, only as the final non-whitespace content. Never use these "
        "markers to run commands or request opening/closing tabs. Attribute evidence to its "
        "source and distinguish observations from inference.\n\nUSER REQUEST:\n%s", cleaned_text);
    td->terminal_context = terminal_context;
    g_thread_unref(g_thread_new("ai_worker", (GThreadFunc)ai_thread_func, td));
    g_free(cleaned_text);

    gtk_entry_set_text(entry, "");

}

void update_status_label(AppContext *app, const char *status) {
    if (app && app->gui.status_label) {
        gtk_label_set_text(GTK_LABEL(app->gui.status_label), status);
    }
}

// New helper function to handle user interaction
gboolean request_human_approval(AppContext *app, const char *input_text) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->gui.window),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Security Alert: Command Requires Approval\n\nCommand: %s\n\nProceed with execution?", 
                                    input_text);
    gtk_window_set_title(GTK_WINDOW(dialog), "Manual Approval Required");
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return (result == GTK_RESPONSE_OK);
}

// Inside update.c
gboolean refresh_token_display(gpointer data) {
    AppContext *app = (AppContext *)data;
    if (!app || !app->tokens.bar) return FALSE; // Guard clause

    // 1. Calculate and update fill progress fraction
    double fraction = 0.0;
    if (app->tokens.max > 0) {
        fraction = (double)app->tokens.current / (double)app->tokens.max;
        if (fraction > 1.0) fraction = 1.0;
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->tokens.bar), fraction);

    // 2. Format and commit string live
    char tracking_str[128];
    snprintf(tracking_str, sizeof(tracking_str), 
             "Current Tokens: %ld \t Last AI Process Used: %ld Tokens", 
             app->tokens.current, app->tokens.last);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->tokens.bar), tracking_str);

    return FALSE; // Vital: tells GTK not to loop this function indefinitely
}
