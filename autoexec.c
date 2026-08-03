// part of aiterm project
// autoexec.c - Updated with Wildcard & Interactive Fallback Prompt
// By: Peter Talbott
// Assisted by: Gemini
// July 2026

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

/**
 * Identifies standard shell keywords and built-in commands.
 */
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

/**
 * Verifies if a binary name corresponds to a real executable in $PATH,
 * a built-in shell keyword, or an explicit file path.
 */
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

/**
 * Extracts the primary executable binary name from a command line.
 */
char* extract_binary_name(const char *cmd_line) {
    if (!cmd_line) return NULL;

    while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
    if (*cmd_line == '\0') return NULL;

    const char *start = cmd_line;
    while (*cmd_line && !isspace((unsigned char)*cmd_line)) cmd_line++;

    return g_strndup(start, cmd_line - start);
}

/**
 * Parses markdown code blocks (```bash ... ```) from AI responses.
 */
GList* extract_code_blocks(const char *text) {
    GList *commands = NULL;
    if (!text) return NULL;

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
    return commands;
}

/**
 * Injects approved command bytes into the VTE child shell.
 */
void feed_command_to_vte(AppContext *app, const char *cmd) {
    if (!VTE_IS_TERMINAL(app->gui.terminal_view)) return;

    char *exec_str = g_strdup_printf("%s\n", cmd);
    VteTerminal *vte = VTE_TERMINAL(app->gui.terminal_view);

    vte_terminal_feed_child(vte, exec_str, strlen(exec_str));
    DEBUG_PRINT("[AUTOEXEC]: Sent to VTE: %s", exec_str);
    g_free(exec_str);
}

void process_auto_execution(AppContext *app, const char *ai_text) {
    if (!app || !app->sys.auto_execute_enabled || !ai_text) return;

    GList *blocks = extract_code_blocks(ai_text);
    if (!blocks) return;

    for (GList *b = blocks; b != NULL; b = b->next) {
        char *block_text = (char *)b->data;

        // Process code blocks line by line
        char **lines = g_strsplit(block_text, "\n", -1);

        for (int i = 0; lines[i] != NULL; i++) {
            char *line = g_strstrip(lines[i]);

            // Skip empty lines, comments, and output artifacts
            if (strlen(line) == 0 || line[0] == '#' || g_str_has_prefix(line, "//") || is_output_artifact(line)) {
                continue;
            }

            char *binary = extract_binary_name(line);
            if (!binary) continue;

            // STRICT GUARD: Must be a verified executable or shell builtin
            if (!is_valid_executable(binary)) {
                DEBUG_PRINT("[AUTOEXEC]: Ignored non-executable string: '%s'", binary);
                g_free(binary);
                continue; // Silently drop non-commands without triggering approval dialogs!
            }

            // Query policy database for verified binary
            PolicyRecord *p = get_policy_for_command(app, binary);
            if (!p) {
                p = get_policy_for_command(app, "*"); // Fallback to wildcard rule
            }

            const char *action_type = p ? p->type : "APPROVE";

            if (g_ascii_strcasecmp(action_type, "ALLOW") == 0) {
                char *banner = g_strdup_printf("[Auto-Execute ALLOW]: %s", line);
                write_to_ai_pane(app, "System: ", banner, "system_tag", "body_tag");
                g_free(banner);

                feed_command_to_vte(app, line);
            } 
            else if (g_ascii_strcasecmp(action_type, "BLOCK") == 0) {
                char *msg = g_strdup_printf("Execution blocked by security policy ('%s')", binary);
                write_to_ai_pane(app, "[Policy Blocked]: ", msg, "ai_tag", "body_tag");
                g_free(msg);
            } 
            else { // "APPROVE" for valid commands
                char *msg = g_strdup_printf("Approval requested for command '%s'", binary);
                write_to_ai_pane(app, "[Policy Hold]: ", msg, "cmd_tag", "body_tag");

                // Only pop approval dialog for VALID commands requiring review
                if (request_human_approval(app, line)) {
                    write_to_ai_pane(app, "System: ", "Approved by user. Executing...", "system_tag", "body_tag");
                    feed_command_to_vte(app, line);
                } else {
                    write_to_ai_pane(app, "System: ", "Execution cancelled.", "ai_tag", "body_tag");
                }
            }

            if (p) free_policy_record(p);
            g_free(binary);
        }

        g_strfreev(lines);
        g_free(block_text);
    }

    g_list_free(blocks);
}

