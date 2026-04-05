#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include "help.h"

const char* get_version_info() {
    return
        "AI-Term GTK v0.4\n"
        "Location: Weston, WV\n"
        "Platform: UbuntuMini\n"
        "Features:\n"
        "  - GTK Terminal + AI Panel\n"
        "  - OpenAI Integration\n"
        "  - AI Tee Mode (Buffered Analysis)\n"
        "  - Menu System\n";
}

const char* get_help_text() {
    return
        "--- AI-Term GTK v0.2 Commands ---\n"
        "\n"
        "Core:\n"
        "  help        : Show this menu\n"
        "  clear       : Clear the AI pane\n"
        "  exit        : Close the application\n"
        "  version     : Show version info\n"
        "\n"
        "System:\n"
        "  hw          : Show system hardware stats\n"
        "\n"
        "AI Features:\n"
        "  tee on      : Enable AI Tee mode\n"
        "  tee off     : Disable AI Tee mode\n"
        "  tee flush   : Send buffered output to AI\n"
        "\n"
        "Notes:\n"
        "  - AI responses require a valid OpenAI API key\n"
        "  - Tee mode sends terminal output for analysis\n";
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
