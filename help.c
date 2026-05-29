// part of aiterm project
// help.c
// different functions for displaying various help info
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include "help.h"
#include "utils.h"

// Reference to global version defined in main.c
extern const char* AITERM_VERSION;

const char* FEATURES =
    "Features:\n"
    "  - Atomic 'Snapshot & Clear' Capture Pipeline (Race-Condition Proof)\n"
    "  - Dynamic SQL Allocation Engine (Supports unlimited terminal dumps)\n"
    "  - Deep Context 'Smart History' (100-message lookback via MariaDB)\n"
    "  - Multi-Tab VTE Architecture with isolated pointer synchronization\n"
    "  - AI Integration (OpenAI/Gemini) with persistent analysis logging\n";

const char* get_version_info() {
    static char v_buf[512];
    snprintf(v_buf, sizeof(v_buf), "AI-Term GTK v%s\nLocation: Weston, WV\n%s", AITERM_VERSION, FEATURES);
    return(v_buf);
}

const char* get_features_text() {
    get_version_info();
    return
        "\n"
        "      *** Changes in Version 0.8.4-alpha (Current) ***\n"
	"      - [New] Policy Manipulation to govern AI commands.\n"
        "      - [New] Pre-flight Auto-Migration: Executing 6 distinct schema queries safely on startup.\n"
        "      - [Fixed] DB Boot Deadlock: UI main loop decoupled from remote MariaDB infrastructure routines.\n"
        "      - [Secure] RAM Sanitization: Master crypt keys strictly zeroed out in memory before deallocation.\n"
	"      - [Secure] Replaced weak XOR-HEX crypto with AES-256-CBC encryption of mysql password and AI key stored in config.\n"
        "      - [Fixed] GtkNotebook CSS Overrides: Targeted selectors patch white-theme clipping for root execution.\n"
        "      - [Network] Fail-Safe Boundaries: Added strict 10s connect and 30s request transfer timeouts.\n"
        "\n"
        "      *** Changes in Version 0.8.2-stable ***\n"
        "      - [New] Atomic Capture: Mutex-wrapped VTE reads eliminate splicing bugs.\n"
        "      - [New] Pointer Sync: Monotonic row-tracking prevents duplicate AI flushes.\n"
        "      - [New] Dynamic SQL: Query buffers now scale to terminal output size.\n"
        "      - [New] Deep Memory: All history functions expanded to 100-message context.\n"
        "      - [Fixed] Persistence: Auto-Reply analysis is now correctly saved to DB.\n"
        "      - [Fixed] ANSI Engine: State-machine correctly handles 0x40-0x7E CSI range.\n"
        "\n"
        "      *** Changes in Version 0.8.0-alpha/beta ***\n"
        "      - [New] Multi-Tab Interface powered by GtkNotebook layout matrix.\n"
        "      - [New] Dynamic Right-Click Context Menu (New Tab, Copy, Paste).\n"
        "      - [New] Lifecycle Tracking syncing active shell variables on tab flips.\n"
        "      - [New] Adaptive Font/Opacity inheritance across tabs.\n"
        "\n"
        "      *** Past Core Improvements & Milestones ***:\n"
        "      - [New] File Analysis: Paperclip icon context injection (.c, .h, .log).\n"
        "      - [New] Cyber-Eye branding via optimized GResource compressed assets.\n"
        "      - [Fixed] Persistent MariaDB connection recovery and auto-migration.\n"
        "      - [Secure] Hex-XOR password data obfuscation for aiterm.conf.\n"
        "\n"
        "Notes:\n"
        "  - AI responses require a valid OpenAI/Gemini API key.\n"
        "  - Tee mode captures terminal output history securely for analysis.\n";
}

const char* get_help_text() {

    get_version_info();
    int written=0;
    int len=0;

    char *buffer = calloc(1, HELP_BUFFER_SIZE);
    if (!buffer)
        return NULL;

    const char *help_array[] = {
        "Core GUI Shell Commands", " ",
        "help", "Show this console interactive menu",
        "hw", "Show basic hardware specs",
        "clear", "Clear the AI history layout view",
        "status", "Display application operational toggles",
        "history", "Display historical logs cached in MariaDB",
	"save config", "Save settings to aiterm.conf",
	"load config", "Load settings from aiterm.conf",
        "exit", "Close the application instance safely",
        "Keyboard Shortcuts:", " ",
        "Ctrl + Tab", "Cycle focus (Input -> Active Tab -> AI View)",
        "Mouse Interactions:", " ",
        "Right-Click", "Pop up context menu (New Tab / Copy / Paste)",
        "AI on/off Controls:", " ",
        "tee", "Toggle immediate terminal capturing",
        "autoreply", "Toggle real-time prompt analysis",
        "auto execute", "Toggle direct execution of AI payloads",
        "auto", "Toggle all three at once",
        "UI Utility Toolbar Buttons:", " ",
        "Paperclip", "Launch file-chooser to upload to AI",
        "Copy Icon", "Copy the complete AI string contents",
	"Policy Manipulation", " ",
	"policy", " policy <function>",
	"Policy add", "policy ADD <cmd> <type> <risk>",
	"policy delete", "policy DELETE <cmd>",
	"policy list", "policy LIST",
	"policy find", "policy FIND <cmd>",
        "Context Pipe Blurb", " ",
        "Piped Triage", "Invoking 'status' outputs runtime metrics.",
	" ", "When automated parameters are toggled ON,",
        " ", "this layout stream evaluates engine state anomalies",
        " ", "(e.g., credit exhaustion) and passes context",
        " ", "boundaries dynamically up to the remote AI triage layer."
    };

    int length = sizeof(help_array) / sizeof(help_array[0]);
    int offset = 0;

    for (int i = 0; i < length - 1; i += 2) {

	if (strcmp(help_array[i + 1], " ") == 0) {
	    written = snprintf(
	    buffer + offset,
	    HELP_BUFFER_SIZE - offset,
            "\n=== %s ===\n",
            help_array[i]
            );
	} else {
	    len = strlen( help_array[i] );
	    if (len < 11) {
                written = snprintf(
                buffer + offset,
                HELP_BUFFER_SIZE - offset,
                "%-22s\t%s\n",
                help_array[i],
                help_array[i + 1]
                );
	    } else {
                written = snprintf(
                buffer + offset,
                HELP_BUFFER_SIZE - offset,
                "%-22s %s\n",
                help_array[i],
                help_array[i + 1]
                );
	    }
	}

        if (written < 0 || offset + written >= HELP_BUFFER_SIZE)
            break;

        offset += written;
    }

    return buffer;
}

char* get_hw_stats() {
    struct sysinfo info;
    struct utsname os_info;

    char *stats = malloc(512);
    if (!stats) return NULL;

    if (sysinfo(&info) != 0 || uname(&os_info) != 0) {
        snprintf(stats, 512, "Error retrieving hardware stats.");
        return stats;
    }

    // Calculate RAM in MB
    unsigned long total_ram = (info.totalram * info.mem_unit) / 1024 / 1024;
    unsigned long free_ram = (info.freeram * info.mem_unit) / 1024 / 1024;

    snprintf(stats, 512,
        "--- Hardware Stats ---\n"
        "OS: %s %s\n"
        "Kernel: %s\n"
        "Uptime: %ld hours\n"
        "Total RAM: %lu MB\n"
        "Free RAM: %lu MB",
        os_info.sysname, os_info.machine,
        os_info.release,
        info.uptime / 3600,
        total_ram, free_ram
    );

    return stats;
}

const char* get_cmd_help() {
    return
        "Usage: aiterm [OPTIONS]\n\n"
        "Options:\n"
        "  --help\t\t\tShow this help menu\n"
        "  --version\t\t\tDisplay current version\n"
        "  --debug\t\t\tEnable verbose debug logging to stderr\n"
        "  --features\t\t\tShow details on features supported by current version\n"
        "  --master=<key>\t\tProvide the master password directly to decrypt saved config options\n"
        "  --crypt-pw=<password>\t\tEncrypt a plaintext password to AES-256-CBC encryption for aiterm.conf\n\n"
        "Environment Variables:\n"
        "  AITERM_MASTER_KEY\t\tFallback variable evaluated if the --master option flag is missing\n\n"
        "Examples:\n"
        "  ./aiterm --master='secretkey' --debug\n"
        "  export AITERM_MASTER_KEY='secretkey' && ./aiterm\n";
}
