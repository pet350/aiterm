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
extern const char* AITERM_BUILD_TIME;

const char* HIGHLIGHT_STRING =
    "Application Highlights:\n"
    "  - Atomic 'Snapshot & Clear' Capture Pipeline (Race-Condition Proof)\n"
    "  - Dynamic SQL Allocation Engine (Supports unlimited terminal dumps)\n"
    "  - Deep Context 'Smart History' (100-message lookback via MariaDB)\n"
    "  - Multi-Tab VTE Architecture with isolated pointer synchronization\n"
    "  - AI Integration (OpenAI/Gemini) with persistent analysis logging\n"
    "  - Contact: Peter Talbott\t\t\tpet.350.pt@gmail.com\n";


const char* get_version_info() {
    static char v_buf[768];
    snprintf(v_buf, sizeof(v_buf), "AI-Term GTK Version: %-16s\nBuild ID: %s\nBuild Time: %s\nLocation: Weston, WV\n%s", AITERM_VERSION, AITERM_BUILDID, AITERM_BUILD_TIME, HIGHLIGHT_STRING);
    return(v_buf);
}

const char* get_features_text() {
    get_version_info();
    return
        "\n"
	"    AITerm available on GitHub. you can clone it at https://github.com/pet350/aiterm.git. \n"
	"\n"
	"\t  These have been quite a few Enhancements, Upgrades, New features, and bugfixes since my last release.\n"
	"\t  Anyone seeing this post should go ahead and clone it, build it, check it out and let me know what you\n"
	"\t  all think of it. Some of the New features and bugfixes are:\n"
	"\n"
	"\t- This project is completely written in C. it is lightweight, very quick and responsive,\n"
	"\t  it maintains one connection to MySQL that is threaded and locks the mutex when being accessed,\n"
	"\t  so only function in one thread can access it at once. This way it does not open multiple connections\n"
	"\t  and waist your MySQL resources. even performing all those steps it does amazingly fast and\n"
	"\t  without any problems that I've noticed. I did add a reset feature that I never really need to\n"
	"\t  use but it is there incase it hangs for whatever reason.   \n"
	"\n"
	"\t- Totally reworked the 'Tee' function, now AI doesn't miss a line from the terminal window\n"
	"\t  (as long as tee is enabled) \n"
	"\n"
	"\t- Bugfix: addressed the 'Blank Lines' that would get sent to AI while tee is enabled.\n"
	"\t  Some of the causes but not limited to, when the screen would be cleared, AI would get 30 or so\n"
	"\t  blank lines sent to it. and Gemini let me know how much she does not like receiving\n"
	"\t  these blank lines. Well, I added a function the detects and strips out all blank lines prior to\n"
	"\t  sending to AI. Gemini thanked me for the addition of this feature that is always checking the\n"
	"\t  buffer of the send queue for blank lines prior to transmitting. \n"
	"\n"
	"\t- Any function that can be performed from the local command input can also be performed through\n"
	"\t  the gui drop down menu\n"
	"\n"
	"\t- Session management. All data being logged to MySQL is associated with a session now that can\n"
	"\t  be managed from local commands or the newly added Session Manager Window where it is easy to\n"
	"\t  add, load , or delete sessions.\n"
	"\n"
	"\t- More modular structure, you will notice quite a few more .c and .h files.\n"
	"\t  Functions that have a common purpose are now grouped into their own .c file.\n"
	"\t  (Instead of how I had it, an ever increasing utils.c LOL! at one point that file was well over\n"
	"\t  3000 lines of code. That is when I decided to take the known working well functions, group them\n"
	"\t  and split them off into their own respective .c and .h files.)\n"
	"\n"
	"\t- Created a structure for the AI provider, making it much easier to switch AI providers. Instead\n"
	"\t  of having each provider hard coded and creating new functions for each provider, I made it simple\n"
	"\t  to change providers between OpenAI / Gemini / and probably much more (only tested with those two,\n"
	"\t  but I'm sure using the provider structure I have setup it should be trivial to add another provider)\n"
	"\n"
	"\t- Auto Execution and Rate limiting have been added, but they still have some bugs to work out.\n"
	"\t  Would love some feedback from you all on fixes for these features. \n"
	"\n"
	"\t- Bugfix / CSS. Forced a dark theme for all windows used in this project.\n"
	"\n"
	"\t- All AI transactions and MySQL transactions are asynchronous and threaded, so nothing will hang the\n"
	"\t  terminal up while processing. You could just keep on using the terminal just like you would with\n"
	"\t  any other terminal emulator without any hesitation from the GTK gui at all. Like you would never\n"
	"\t  know that SQL transactions and AI Thinking was even happening. \n"
	"\n"
	"\t- All AI communications are assembled/disassembled by LibJson and the send and receive is handled\n"
	"\t  by libCurl\n"
	"\n"
	"\t- Each time it gets built a new UUID gets associated with that build as well as a timestamp, for\n"
	"\t  making it easier to keep tract of.\n"
	"\n"
	"\t- All UUIDs are handled by libuuid\n"
	"\n"
	"\t- when ./aiterm --debug is invoked pleanty of debug output gets written to that calling terminal\n"
	"\t  window. Great / needed for troubleshooting. \n"
	"\n"
	"\t- when ./aiterm --help is invoked all the command line switches are detailed. (there are only a few\n"
	"\t  at the moment, but how many more do you need... lol)\n"
	"\n"
	"\t- Please note: for initial config setup you will need a master key to create the encrypted password\n"
	"\t  and key needed in the config file. that means at minimum two command line options are needed:\n"
	"\t  ./aiterm --master=(your master encryption key you make up)  --crypt-pw=(the password or key you\n"
	"\t  want encrypted) the output of this command line is what you will put in the config file. you can\n"
	"\t  run the above command with --debug as well if you want more printed to the screen. \n"
	"\n"
    	"\t- Database creation: originally I was having the application check for and create if not present\n"
	"\t  the database but this has proven to be more of a nightmare than anticipated. so I included a .sql\n"
	"\t  file that will create the DB for you. You will still need to create a user that has access to this\n"
	"\t  database. only one with GRANT option has been tested, but I think a lesser account would probably\n"
	"\t  work just fine. \n"
	"\n"
	"\t  *** Changes in Version 0.8.5-alpha (Current) ***\n"
	"\t- [New] XML Telemetry Protocol: Live terminal state (<tee>) and DB memory (<history>) \n"
	"\t  now wrapped in formal semantic tags for enhanced AI perception.\n"
	"\t- [New] Session Pinning: Persistent UUID-based session management for AI thread tracking.\n"
	"\t- [New] Silent-Boot Infrastructure: OS-level shell noise suppression ensures AI context \n"
	"\t  receives only user-generated terminal output.\n"
	"\t- [New] IO/Buffer Protection: Sliding-window accumulation (64KB) prevents context \n"
	"\t  window overflow and protects against massive IO floods (NFS/Kernel Panics).\n"
	"\t- [Fixed] Terminal Ghosting: VTE scrollback/newline initialization plague eliminated \n"
	"\t  via atomic cursor/buffer synchronization.\n"
	"\t- [Performance] Decoupled Logging: Asynchronous XML-tagging occurs outside the UI \n"
	"\t  thread to maintain < 5ms latency during heavy stream processing.\n"
	"\t- [Secure] Protocol Sanitization: Automatic Unicode/JSON escaping ensures valid \n"
	"\t  transmission to remote API endpoints.\n"
	"\n"
        "\t *** Changes in Version 0.8.4-alpha ***\n"
	"\t- [New] Policy Manipulation to govern AI commands.\n"
        "\t- [New] Pre-flight Auto-Migration: Executing 6 distinct schema queries safely on startup.\n"
        "\t- [Fixed] DB Boot Deadlock: UI main loop decoupled from remote MariaDB infrastructure routines.\n"
        "\t- [Secure] RAM Sanitization: Master crypt keys strictly zeroed out in memory before deallocation.\n"
	"\t- [Secure] Replaced weak XOR-HEX crypto with AES-256-CBC encryption of mysql password and AI key stored in config.\n"
        "\t- [Fixed] GtkNotebook CSS Overrides: Targeted selectors patch white-theme clipping for root execution.\n"
        "\t- [Network] Fail-Safe Boundaries: Added strict 10s connect and 30s request transfer timeouts.\n"
        "\n"
        "\t *** Changes in Version 0.8.2-stable ***\n"
        "\t- [New] Atomic Capture: Mutex-wrapped VTE reads eliminate splicing bugs.\n"
        "\t- [New] Pointer Sync: Monotonic row-tracking prevents duplicate AI flushes.\n"
        "\t- [New] Dynamic SQL: Query buffers now scale to terminal output size.\n"
        "\t- [New] Deep Memory: All history functions expanded to 100-message context.\n"
        "\t- [Fixed] Persistence: Auto-Reply analysis is now correctly saved to DB.\n"
        "\t- [Fixed] ANSI Engine: State-machine correctly handles 0x40-0x7E CSI range.\n"
        "\n"
        "\t*** Changes in Version 0.8.0-alpha/beta ***\n"
        "\t- [New] Multi-Tab Interface powered by GtkNotebook layout matrix.\n"
        "\t- [New] Dynamic Right-Click Context Menu (New Tab, Copy, Paste).\n"
        "\t- [New] Lifecycle Tracking syncing active shell variables on tab flips.\n"
        "\t- [New] Adaptive Font/Opacity inheritance across tabs.\n"
        "\n"
        "\t*** Past Core Improvements & Milestones ***:\n"
        "\t- [New] File Analysis: Paperclip icon context injection (.c, .h, .log).\n"
        "\t- [New] Cyber-Eye branding via optimized GResource compressed assets.\n"
        "\t- [Fixed] Persistent MariaDB connection recovery and auto-migration.\n"
        "\t- [Secure] Hex-XOR password data obfuscation for aiterm.conf.\n"
        "\n"
        "\tNotes:\n"
        "\t- AI responses require a valid OpenAI/Gemini API key.\n"
        "\t- Tee mode captures terminal output history securely for analysis.\n";
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
        "legacy help", "Show this list of extended commands",
        "exit", "Close the application instance safely",
        "Keyboard Shortcuts:", " ",
        "Ctrl + Tab", "Cycle focus (Input -> Active Tab -> AI View)",
        "Mouse Interactions:", " ",
        "Right-Click", "Pop up context menu (New Tab / Copy / Paste)",
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
	"  --list-models\t\t\tLists available Gemini models\n"
        "  --crypt-pw=<password>\t\tEncrypt a plaintext password to AES-256-CBC encryption for aiterm.conf\n\n"
        "Environment Variables:\n"
        "  AITERM_MASTER_KEY\t\tFallback variable evaluated if the --master option flag is missing\n\n"
        "Examples:\n"
        "  ./aiterm --master='secretkey' --debug\n"
        "  export AITERM_MASTER_KEY='secretkey' && ./aiterm\n";
}
