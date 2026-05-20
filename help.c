// part of aiterm project
// help.c
// different functions for displaying various help info
// By: Peter Talbott
// Assisted by: Gemini
// May 2026

#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include "help.h"
#include "utils.h"

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
        "      *** Changes in Version 0.8.2-stable (Current) ***\n"
        "      - [New] Atomic Capture: Mutex-wrapped VTE reads eliminate splicing bugs\n"
        "      - [New] Pointer Sync: Monotonic row-tracking prevents duplicate AI flushes\n"
        "      - [New] Dynamic SQL: Query buffers now scale to terminal output size\n"
        "      - [New] Deep Memory: All history functions expanded to 100-message context\n"
        "      - [Fixed] Persistence: Auto-Reply analysis is now correctly saved to DB\n"
        "      - [Fixed] ANSI Engine: State-machine correctly handles 0x40-0x7E CSI range\n"
        "\n"
        "      *** Changes in Version 0.8.0-alpha/beta ***\n"
        "      - [New] Multi-Tab Interface powered by GtkNotebook layout matrix\n"
        "      - [New] Dynamic Right-Click Context Menu (New Tab, Copy, Paste)\n"
        "      - [New] Lifecycle Tracking syncing active shell variables on tab flips\n"
        "      - [New] Adaptive Font/Opacity inheritance across tabs\n"
        "\n"
        "      *** Past Core Improvements & Milestones ***:\n"
        "      - [New] File Analysis: Paperclip icon context injection (.c, .h, .log)\n"
        "      - [New] Cyber-Eye branding via optimized GResource compressed assets\n"
        "      - [Fixed] Persistent MariaDB connection recovery and auto-migration\n"
        "      - [Secure] Hex-XOR password data obfuscation for aiterm.conf\n"
        "\n"
        "Notes:\n"
        "  - AI responses require a valid OpenAI/Gemini API key\n"
        "  - Tee mode captures terminal output history securely for analysis\n";
}

const char* get_help_text() {
    get_version_info();
    return
        "\n"
        "Core Shell Commands:\n"
        "  help\t\t: Show this console interactive menu\n"
        "  clear\t\t: Clear the AI history layout view\n"
        "  status\t\t: Display application operational toggles\n"
        "  history\t\t: Display historical logs cached in MariaDB\n"
        "  exit\t\t: Close the application instance safely\n"
        "\n"
        "Keyboard Shortcuts:\n"
        "  Ctrl + Tab\t: Cycle focus (Input -> Active Tab -> AI View)\n"
        "\n"
        "Mouse Interactions:\n"
        "  Right-Click\t: Pop up context menu (New Tab / Copy / Paste)\n"
        "\n"
        "AI Controls:\n"
        "  tee on/off\t: Toggle immediate backplane terminal stream capturing\n"
        "  autoreply on: Enable dynamic prompt generation rules on system activity\n"
        "\n"
        "UI Utility Toolbar Buttons:\n"
        "  Paperclip\t: Launch file-chooser to inject log/code context\n"
        "  Copy Icon\t: Copy the complete right-pane AI string contents\n";
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
        "  --help\t\tShow this help menu\n"
        "  --version\t\tDisplay current version\n"
        "  --debug\t\tEnable verbose debug logging to stderr\n"
        "  --features\t\tShow details on features supported by current version\n"
        "  --crypt-pw=<password>\tEncrypt a plaintext password to XOR-HEX for aiterm.conf\n\n"
        "Example:\n"
        "  ./aiterm --crypt-pw=mypassword123\n";
}
