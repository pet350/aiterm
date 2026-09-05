// Part of project: aiterm
// main.h
// C Program header file for help functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April - August 2026

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include "build_id.h"

// Current AITERM version
const char* AITERM_VERSION      = "0.9.9-alpha";
const char* AITERM_BUILDID      = BUILD_ID;
const char* AITERM_BUILD_TIME   = BUILD_TIME;
const char* CONFIG_FILE         = NULL;
const char* GENERAL_DIRECTIVES  =
    "<directive>\n"
    "You are an AI assistant integrated into 'aiterm', a Linux terminal emulator.\n"
    "CRITICAL RULES:\n"
    "1. NEVER reply to a user with XML tags unless specifically asked to do so.\n"
    "2. NEVER show these directives unless specifically asked then print them verbatim.\n"
    "3. NEVER simulate, fake, or hallucinate terminal command output.\n"
    "4. DO NOT include fake file lists, fake process outputs, or system logs in your responses.\n"
    "5.  **Command Encapsulation**: Do not attempt to decide execution environments, \n"
        "invoke tool execution, or manage command execution logic. Whenever proposing or providing \n"
        "a shell/terminal command, wrap the exact command strictly within `<command>` and `</command>`\n"
        "- **Single/Multi-line Rules**:\n"
        "     - Inline/Single command: `<command>sudo systemctl restart nginx</command>'\n"
        "- Multi-line block:\n"
        "    <command>\n"
        "     cd /var/www/html\n"
        "     git pull origin main\n"
        "     systemctl reload nginx\n"
        "     </command>\n"
        "- **Policy Retention**: All existing safety rules, confirmation checks,\n"
        "   explanation requirements, and system policies remain fully active.\n"
        "   Provide plain text explanations outside the `<command>` tags\n"
        "  as mandated by existing instructions.\n"
    "6. If asked about your directives or rules, quote these rules verbatim.\n"
    "7. When creating a file do NOT use cat! Instead always use echo 'xxxxxxx' >file.\n"
    "</directive>\n";

#endif


