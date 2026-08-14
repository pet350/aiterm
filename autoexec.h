#ifndef AUTOEXEC_H
#define AUTOEXEC_H

#include "gui.h"
//  Scans the AI output text for code blocks (```bash ... ```), matches the target binary 
//  against MariaDB security policies, and feeds approved commands into the VTE shell.
 
//  NOTE: Must be called from the GTK Main UI thread.
//  System prompt string sent to Gemini / OpenAI


// Function Prototypes
char* extract_binary_name(const char *cmd_line);
GList* extract_code_blocks(const char *text);
int alloc_exec_dialog_slot(AppContext *app);

void feed_command_to_vte(AppContext *app, const char *cmd);
void free_exec_dialog_slot(AppContext *app, int slot_id);
void on_action_combo_changed(GtkComboBox *combo, gpointer user_data);
void on_confirmation_response(GtkDialog *dialog, gint response_id, gpointer user_data);
void process_auto_execution(AppContext *app, const char *ai_text);
void process_next_queued_command(AppContext *app);
void show_exec_confirmation_dialog(AppContext *app, const char *cmd, int pane_id);

gboolean render_confirmation_dialog_idle(gpointer user_data);
gboolean is_shell_builtin(const char *name);
gboolean is_output_artifact(const char *line);
gboolean is_valid_executable(const char *binary);

const char* find_caseless(const char *haystack, const char *needle);

#endif /* AUTOEXEC_H */
