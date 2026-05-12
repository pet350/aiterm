// part of aiterm project
// help.c
// different functions for displaying various help info
// By: Peter Talbott
// Assisted by: Gemini
// April 2026

#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include "help.h"
#include "utils.h"


const char* FEATURES="Features:\n  - GTK Terminal + AI Panel\n  - AI Integration (OpenAI/Gemini)\n  - AI Tee Mode (Buffered Analysis)\n  - Menu System\n";


const char* get_version_info() {
    static char v_buf[512];
    snprintf(v_buf, sizeof(v_buf), "AI-Term GTK v%s\nLocation: Weston, WV\n%s", AITERM_VERSION, FEATURES);
    return(v_buf);
}

const char* get_features_text() {
    get_version_info();
    return
        "\n"
        "      *** Changes in Version 0.7.5-beta (Current) ***\n"
        "      - [New] Embedded 'Cyber-Eye' branding via GResource\n"
        "      - [New] File Analysis: Upload local files via paperclip icon\n"
        "      - [New] Clipboard Integration: One-click copy of AI history\n"
        "      - [New] Smart Focus: Ctrl+Tab circular navigation (Term->AI->Input)\n"
        "      - [Fixed] Optimized binary size (Resource compression 45MB -> 100KB)\n"
        "      - [Fixed] API Error Trapping: Human-readable 429/500 reporting\n"
        "\n"
        "      *** Recent Stability Improvements ***:\n"
        "      - [Fixed] Persistent MariaDB connection\n"
        "      - [Fixed] VTE Binary Stream resilience (Crash-proof)\n"
        "      - [Fixed] Thread-safe STDOUT/STDERR interleaving\n"
        "      - [Fixed] SQL Schema auto-repair on startup\n"
        "\n"
        "      *** Current Capabilities: ***\n"
        "      - AI Tee Mode: Buffered terminal analysis\n"
        "      - File Analysis: Analyze .c, .h, and log files directly\n"
        "      - Smart History: Relevance-based keyword filtering\n"
        "      - Security: XOR-Hex encrypted database credentials\n"
        "      - Customization: Transparent UI, Iconography, & Font selection\n"
        "\n"
        "  - Changes in Version 0.7.4-stable\n"
        "      - Status: Production Build\n"
        "\n"
        "      **** Recent Stability Improvements ***:\n"
        "      - [Fixed] Persistent MariaDB connection\n"
        "      - [Fixed] VTE Binary Stream resilience (Crash-proof)\n"
        "      - [Fixed] Thread-safe STDOUT/STDERR interleaving\n"
        "      - [Fixed] SQL Schema auto-repair on startup\n"
        "\n"
        "      *** Current Capabilities: ***\n"
        "      - AI Tee Mode: Buffered terminal analysis\n"
        "      - Smart History: Relevance-based keyword filtering\n"
        "      - Security: XOR-Hex encrypted database credentials\n"
        "      - Customization: Transparent UI & Font selection\n"
        "\n"
        "  - Changes Version 0.7.3-beta\n"
        "      - Added Hex XOR encryption for database passwords\n"
        "      - Added --help, --version, and --crypt-pw options\n"
        "      - Added Smart History keyword filtering\n"
	 	"\n"
	 	"  - Changes Version 0.7.2-beta\n"
        "      - Added font selection for both terminal and AI pane\n"
        "      - Added save/load settings to aiterm.conf\n"
        "\n"
        "  - Changes Version 0.7.1-beta\n"
        "      - Added Transparency\n"
        "\n"
        "  - Changes Version 0.7-stable\n"
        "      - MySQL history corrected\n"
        "      - Multithreaded Terminal pane independent from AI pane\n"
 		"\n"       
        "Notes:\n"
        "  - AI responses require a valid OpenAI/Gemini API key\n"
        "  - Tee mode sends terminal output for analysis\n";
}



const char* get_help_text() {
    get_version_info();
    return
        "\n"
        "Core:\n"
        "  help        : Show this menu\n"
        "  clear       : Clear the AI pane\n"
        "  exit        : Close the application\n"
        "  version     : Show version info\n"
	"  features    : Show Features\n"
        "\n"
        "System:\n"
        "  hw          : Show system hardware stats\n"
	"  history     : Display history from MYSQL\n"
	"  status      : Display aiterm status\n"
        "\n"
        "AI Features:\n"
        "  tee on      : Enable AI Tee mode\n"
        "  tee off     : Disable AI Tee mode\n"
        "  tee flush   : Send buffered output to AI\n";
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

// help.c
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


