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
void process_auto_execution(AppContext *app, const char *ai_text);
void feed_command_to_vte(AppContext *app, const char *cmd);

#endif /* AUTOEXEC_H */

