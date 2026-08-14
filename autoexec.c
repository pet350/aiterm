// part of aiterm project
// autoexec.c - Updated with Wildcard & Interactive Fallback Prompt
// By: Peter Talbott
// Assisted by: Gemini
// August 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "gui.h"
#include "autoexec.h"
#include "policy_dao.h"
#include "utils.h"
#include "update.h"
#include "commands.h"

// Internal context wrapper passed through g_idle_add to GTK signal callbacks
typedef struct {
    AppContext *app;
    int slot_id;
} DialogSlotContext;

// Returns index of next free slot in app->exec_dialog, or -1 if full
int alloc_exec_dialog_slot(AppContext *app) {
    if (!app) return -1;
    for (int i = 0; i < MAX_DLG; i++) {
        if (!app->exec_dialog[i].active) {
            app->exec_dialog[i].active = TRUE;
            app->exec_dialog[i].slot_id = i;
            app->aiterm_runtime.active_dialog_count++;
            return i;
        }
    }
    return -1; // Queue is full
}

// Clears and resets a specific array slot
void free_exec_dialog_slot(AppContext *app, int slot_id) {
    if (!app || slot_id < 0 || slot_id >= MAX_DLG) return;

    if (app->exec_dialog[slot_id].command_text) {
        g_free(app->exec_dialog[slot_id].command_text);
        app->exec_dialog[slot_id].command_text = NULL;
    }

    app->exec_dialog[slot_id].dialog = NULL;
    app->exec_dialog[slot_id].check_policy = NULL;
    app->exec_dialog[slot_id].combo_action = NULL;
    app->exec_dialog[slot_id].target_pane_id = 0;
    app->exec_dialog[slot_id].active = FALSE;

    if (app->aiterm_runtime.active_dialog_count > 0) {
        app->aiterm_runtime.active_dialog_count--;
    }
}

// Process the next item in the FIFO command queue
void process_next_queued_command(AppContext *app) {
    if (!app || !app->aiterm_runtime.cmd_queue || g_queue_is_empty(app->aiterm_runtime.cmd_queue)) return;

    // Pop the next command at the front of the queue
    char *line = (char *)g_queue_pop_head(app->aiterm_runtime.cmd_queue);
    if (!line) return;

    char *binary = extract_binary_name(line);
    if (!binary || !is_valid_executable(binary)) {
        if (binary) g_free(binary);
        g_free(line);
        // Skip invalid line and immediately check next queued item
        process_next_queued_command(app);
        return;
    }

    PolicyRecord *p = get_policy_for_command(app, binary);
    if (!p) p = get_policy_for_command(app, "*");

    const char *action_type = p ? p->type : "APPROVE";

    if (g_ascii_strcasecmp(action_type, "ALLOW") == 0) {
        char *banner = g_strdup_printf("[Auto-Execute ALLOW]: %s\n", line);
        append_ai_text(app, "System: ", "system_tag");
        append_ai_text(app, banner, "body_tag");
        g_free(banner);

        feed_command_to_vte(app, line);

        if (p) free_policy_record(p);
        g_free(binary);
        g_free(line);

        // Instantly process next command in sequence
        process_next_queued_command(app);
    } 
    else if (g_ascii_strcasecmp(action_type, "BLOCK") == 0 || g_ascii_strcasecmp(action_type, "DENY") == 0) {
        char *msg = g_strdup_printf("Execution blocked by security policy ('%s')\n", binary);
        append_ai_text(app, "[Policy Blocked]: ", "cmd_tag");
        append_ai_text(app, msg, "body_tag");
        g_free(msg);

        if (p) free_policy_record(p);
        g_free(binary);
        g_free(line);

        // Move to next command in sequence
        process_next_queued_command(app);
    } 
    else { // "APPROVE" or missing policy: pause queue and present dialog
        char *msg = g_strdup_printf("Approval requested for command '%s'\n", binary);
        append_ai_text(app, "[Policy Hold]: ", "cmd_tag");
        append_ai_text(app, msg, "body_tag");
        g_free(msg);

        if (p) free_policy_record(p);
        g_free(binary);

        show_exec_confirmation_dialog(app, line, 0);
        g_free(line);
        // Execution sequence pauses here until on_exec_confirm_response triggers the queue again
    }
}

void process_auto_execution(AppContext *app, const char *ai_text) {
    if (!app || !app->sys.auto_execute_enabled || !ai_text) return;

    GList *blocks = extract_code_blocks(ai_text);
    if (!blocks) return;

    if (!app->aiterm_runtime.cmd_queue) {
        app->aiterm_runtime.cmd_queue = g_queue_new();
    }

    for (GList *b = blocks; b != NULL; b = b->next) {
        char *block_text = (char *)b->data;
        char **lines = g_strsplit(block_text, "\n", -1);

        for (int i = 0; lines[i] != NULL; i++) {
            char *line = g_strstrip(lines[i]);

            if (strlen(line) == 0 || line[0] == '#' || g_str_has_prefix(line, "//") || is_output_artifact(line)) {
                continue;
            }

            // Push lines into FIFO queue maintaining exact output order
            g_queue_push_tail(app->aiterm_runtime.cmd_queue, g_strdup(line));
        }

        g_strfreev(lines);
        g_free(block_text);
    }
    g_list_free(blocks);

    // Start processing queue from the head
    process_next_queued_command(app);
}
gboolean render_confirmation_dialog_idle(gpointer user_data) {
    DialogSlotContext *ctx = (DialogSlotContext *)user_data;
    if (!ctx || !ctx->app) return G_SOURCE_REMOVE;

    AppContext *app = ctx->app;
    int slot_id = ctx->slot_id;

    if (slot_id < 0 || slot_id >= MAX_DLG || !app->exec_dialog[slot_id].active) {
        g_free(ctx);
        return G_SOURCE_REMOVE;
    }

    exe_dlg *dlg = &app->exec_dialog[slot_id];

    // NOTE: Removed GTK_DIALOG_MODAL flag so GUI remains responsive
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Security Policy Confirmation",
        GTK_WINDOW(app->gui.window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_REJECT,
        "_Submit", GTK_RESPONSE_ACCEPT,
        NULL
    );

    // Explicitly set window modal property to FALSE
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);

    dlg->dialog = dialog;

    GtkWidget *accept_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_button_set_label(GTK_BUTTON(accept_btn), "Execute Command");

    // Catppuccin Theme styling
    GtkStyleContext *context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(context, "aiterm-dialog");

    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *aiterm_dialog_css = 
        ".aiterm-dialog { background-color: #1e1e2e; color: #cdd6f4; }\n"
        ".aiterm-dialog label { color: #cdd6f4; font-family: 'Monospace', monospace; }\n"
        ".aiterm-dialog checkbutton label { color: #f9e2af; font-weight: bold; }\n"
        ".aiterm-dialog combobox button { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; }\n"
        ".aiterm-dialog button { background-color: #313244; color: #a6e3a1; border: 1px solid #45475a; border-radius: 4px; padding: 6px 16px; font-weight: bold; }\n"
        ".aiterm-dialog button:hover { background-color: #45475a; color: #ffffff; }\n";

    gtk_css_provider_load_from_data(css_provider, aiterm_dialog_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 16);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    
    char *escaped_cmd = g_markup_escape_text(dlg->command_text ? dlg->command_text : "", -1);
    char *markup = g_strdup_printf(
        "<span foreground='#f9e2af' size='large'><b>Security Approval Required</b></span>\n\n"
        "The AI requested to execute the following command:\n\n"
        "<tt><span foreground='#a6e3a1'>%s</span></tt>",
        escaped_cmd
    );
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    g_free(escaped_cmd);

    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 4);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *combo_label = gtk_label_new("Action Policy:");
    GtkWidget *action_combo = gtk_combo_box_text_new();

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "ALLOW (Execute Command)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "BLOCK (Deny Execution)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo), 0);

    gtk_box_pack_start(GTK_BOX(hbox), combo_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), action_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    GtkWidget *remember_check = gtk_check_button_new_with_label("Remember policy choice for this binary");
    gtk_box_pack_start(GTK_BOX(vbox), remember_check, FALSE, FALSE, 0);

    dlg->check_policy = remember_check;
    dlg->combo_action = action_combo;

    g_object_set_data(G_OBJECT(dialog), "remember_check", remember_check);
    g_object_set_data(G_OBJECT(dialog), "action_combo", action_combo);
    g_object_set_data(G_OBJECT(dialog), "slot_id", GINT_TO_POINTER(slot_id));

    g_signal_connect(action_combo, "changed", G_CALLBACK(on_action_combo_changed), accept_btn);
    g_signal_connect(dialog, "response", G_CALLBACK(on_confirmation_response), ctx);

    gtk_widget_show_all(dialog);
    return G_SOURCE_REMOVE;
}

// Identifies standard shell keywords and built-in commands.
gboolean is_shell_builtin(const char *name) {
    if (!name) return FALSE;

    static const char *builtins[] = {
        "cd", "echo", "export", "alias", "source", "set", "unset", 
        "history", "exit", "read", "pwd", "pushd", "popd", "type",
        "if", "then", "else", "fi", "for", "while", "do", "done", "case", "esac",
        NULL
    };

    for (int i = 0; builtins[i] != NULL; i++) {
        if (strcmp(name, builtins[i]) == 0) return TRUE;
    }
    return FALSE;
}

/**
 * Detects common terminal output lines (permissions, totals, headers, prompts)
 * to ensure we never attempt to execute raw terminal output data.
 */
gboolean is_output_artifact(const char *line) {
    if (!line || strlen(line) == 0) return TRUE;

    // Filter file permission strings (e.g., -rw-r--r--, drwxr-xr-x, lrwxrwxrwx)
    if (strlen(line) >= 10 && (line[0] == '-' || line[0] == 'd' || line[0] == 'l' || line[0] == 'c' || line[0] == 'b')) {
        if (line[1] == 'r' || line[1] == '-') return TRUE;
    }

    // Filter CLI headers and shell prompt artifacts
    static const char *output_headers[] = {
        "total ", "Filesystem ", "Size ", "Used ", "Avail ", "Use% ",
        "Mounted ", "PID ", "USER ", "PR ", "NI ", "VIRT ", "RES ",
        "SHR ", "S ", "%CPU ", "%MEM ", "TIME+ ", "COMMAND",
        "opensusevm:", "root@", "user@", "localhost",
        NULL
    };

    for (int i = 0; output_headers[i] != NULL; i++) {
        if (g_str_has_prefix(line, output_headers[i])) return TRUE;
    }

    return FALSE;
}

// Callback to dynamically update button label based on selected policy action
void on_action_combo_changed(GtkComboBox *combo, gpointer user_data) {
    GtkButton *accept_btn = GTK_BUTTON(user_data);
    if (!accept_btn) return;

    int idx = gtk_combo_box_get_active(combo);
    if (idx == 1) { // BLOCK
        gtk_button_set_label(accept_btn, "Block Command");
    } else { // ALLOW
        gtk_button_set_label(accept_btn, "Execute Command");
    }
}

// Async signal handler called when the user interacts with a specific dialog instance
void on_confirmation_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    DialogSlotContext *ctx = (DialogSlotContext *)user_data;
    if (!ctx || !ctx->app) return;

    AppContext *app = ctx->app;
    int slot_id = ctx->slot_id;

    if (slot_id < 0 || slot_id >= MAX_DLG || !app->exec_dialog[slot_id].active) {
        g_free(ctx);
        gtk_widget_destroy(GTK_WIDGET(dialog));
        // Unblock queue if an invalid slot was triggered
        process_next_queued_command(app);
        return;
    }

    exe_dlg *dlg = &app->exec_dialog[slot_id];

    // Retrieve child widget references attached to the dialog object
    GtkWidget *remember_check = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "remember_check"));
    GtkWidget *action_combo = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "action_combo"));

    gboolean save_rule = remember_check ? gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remember_check)) : FALSE;
    int action_idx = action_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(action_combo)) : 0;

    if (response_id == GTK_RESPONSE_ACCEPT && dlg->command_text) {
        // Index 0 = ALLOW, Index 1 = BLOCK
        const char *chosen_action = (action_idx == 1) ? "BLOCK" : "ALLOW";

        if (save_rule) {
            char *binary = extract_binary_name(dlg->command_text);
            if (binary) {
                PolicyRecord rec = {
                    .name = binary,
                    .type = (char *)chosen_action,
                    .risk = 0
                };
                set_command_policy(app, &rec);
                DEBUG_PRINT("[AUTOEXEC]: Persisted policy rule: %s -> %s", binary, chosen_action);
                g_free(binary);
            }
        }

        if (action_idx == 0) { // Execute option
            DEBUG_PRINT("[AUTOEXEC]: User confirmed execution for slot %d: %s", slot_id, dlg->command_text);
            feed_command_to_vte(app, dlg->command_text);
        } else {
            append_ai_text(app, "[Policy Blocked]: Command blocked and saved to policy by user.\n", "cmd_tag");
        }
    } else {
        append_ai_text(app, "[Policy Rejected]: Execution cancelled by user.\n", "cmd_tag");
    }

    // Clean up state stored within app->exec_dialog[slot_id]
    free_exec_dialog_slot(app, slot_id);
    g_free(ctx);

    gtk_widget_destroy(GTK_WIDGET(dialog));

    // Resume queue processing for all remaining commands in app->aiterm_runtime.cmd_queue
    process_next_queued_command(app);
}

void show_exec_confirmation_dialog(AppContext *app, const char *cmd, int pane_id) {
    if (!app || !cmd) return;

    int slot_id = alloc_exec_dialog_slot(app);
    if (slot_id == -1) {
        DEBUG_PRINT("[AUTOEXEC]: Max pending dialog limit (%d) reached! Dropping command: %s", MAX_DLG, cmd);
        append_ai_text(app, "[Error]: Too many pending security dialogs.\n", "cmd_tag");
        return;
    }

    if (app->exec_dialog[slot_id].command_text) {
        g_free(app->exec_dialog[slot_id].command_text);
    }

    app->exec_dialog[slot_id].command_text = g_strdup(cmd);
    app->exec_dialog[slot_id].target_pane_id = pane_id;

    DialogSlotContext *ctx = g_new0(DialogSlotContext, 1);
    ctx->app = app;
    ctx->slot_id = slot_id;

    g_idle_add(render_confirmation_dialog_idle, ctx);
}

// Verifies if a binary name corresponds to a real executable in $PATH,
// a built-in shell keyword, or an explicit file path.
gboolean is_valid_executable(const char *binary) {
    if (!binary || strlen(binary) == 0) return FALSE;

    // 1. Check shell builtins
    if (is_shell_builtin(binary)) return TRUE;

    // 2. Check path references (e.g., ./build.sh or /usr/bin/ls)
    if (strchr(binary, '/') != NULL) {
        return (g_file_test(binary, G_FILE_TEST_IS_EXECUTABLE) && 
               !g_file_test(binary, G_FILE_TEST_IS_DIR));
    }

    // 3. Query system $PATH via GLib
    char *path = g_find_program_in_path(binary);
    if (path) {
        g_free(path); // Executable exists in $PATH!
        return TRUE;
    }

    return FALSE;
}

// Extracts the primary executable binary name from a command line.
char* extract_binary_name(const char *cmd_line) {
    if (!cmd_line) return NULL;

    while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
    if (*cmd_line == '\0') return NULL;

    const char *start = cmd_line;
    while (*cmd_line && !isspace((unsigned char)*cmd_line)) cmd_line++;

    return g_strndup(start, cmd_line - start);
}

// Injects approved command bytes into the VTE child shell.
// Case-insensitive substring search using GLib's g_ascii_strncasecmp
const char* find_caseless(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;

    while (*haystack != '\0') {
        if (g_ascii_strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
        haystack++;
    }
    return NULL;
}

// Parses both markdown code blocks (``` ... ```) and explicit <command>...</command> tags from AI responses.
GList* extract_code_blocks(const char *text) {
    GList *commands = NULL;
    if (!text) return NULL;

    // 1. Extract markdown code blocks (```)
    const char *cursor = text;
    while ((cursor = strstr(cursor, "```")) != NULL) {
        cursor += 3;
        while (*cursor && *cursor != '\n' && !isspace((unsigned char)*cursor)) cursor++;
        while (*cursor && isspace((unsigned char)*cursor) && *cursor != '\n') cursor++;
        if (*cursor == '\n') cursor++;

        const char *end = strstr(cursor, "```");
        if (!end) break;

        size_t len = end - cursor;
        if (len > 0) {
            char *raw_block = g_strndup(cursor, len);
            char *trimmed = g_strstrip(raw_block);
            if (strlen(trimmed) > 0) {
                commands = g_list_append(commands, trimmed);
            } else {
                g_free(raw_block);
            }
        }
        cursor = end + 3;
    }

    // 2. Extract <command>...</command> tags
    cursor = text;
    while ((cursor = find_caseless(cursor, "<command>")) != NULL) {
        cursor += strlen("<command>");
        const char *end = find_caseless(cursor, "</command>");
        if (!end) break;

        size_t len = end - cursor;
        if (len > 0) {
            char *raw_block = g_strndup(cursor, len);
            char *trimmed = g_strstrip(raw_block);
            if (strlen(trimmed) > 0) {
                commands = g_list_append(commands, trimmed);
            } else {
                g_free(raw_block);
            }
        }
        cursor = end + strlen("</command>");
    }

    return commands;
} 

void feed_command_to_vte(AppContext *app, const char *cmd) {
    if (!app || !VTE_IS_TERMINAL(app->gui.terminal_view)) return;

    char *exec_str = g_strdup_printf("%s\n", cmd);
    VteTerminal *vte = VTE_TERMINAL(app->gui.terminal_view);

    vte_terminal_feed_child(vte, exec_str, strlen(exec_str));
    DEBUG_PRINT("[AUTOEXEC]: Sent to VTE: %s", exec_str);
    g_free(exec_str);
}

