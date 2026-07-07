AITerm Ver 0.9.2
 Change Log

[0.9.2] — Current Release
Refactored
Decoupled Architecture: Extracted all engine state-modification routines out of the monolithic commands.c registry file into an isolated, clean subsystem (toggles.c and toggles.h).

Modernized UI Pattern: Discarded the legacy, black-boxed append_ai_action wrapper. Replaced it with an explicit, readable 4-step layout sequence utilizing the new setup_menu_toggle utility for all application keys.

Added
Bidirectional Synchronization: Created a perfect bridge between the terminal shell backend and the GTK frontend. Modifying configurations via terminal text commands now automatically updates the physical state of the GTK check menu items, ensuring your interface never displays stale engine information.

Unified UI Reference Mapping: Embedded permanent, accessible widget tracking pointers directly into a UIComponents tracking struct within AppContext to give the backend an absolute source of truth for the active UI state.

Fixed
Signal Loop Shielding: Fixed a critical infinite feedback loop vulnerability where text-triggered configuration changes would inadvertently cycle back and trigger GUI event listeners. Implemented programmatic state isolation using safe g_signal_handlers_block_by_func() and g_signal_handlers_unblock_by_func() boundaries during internal updates.

[0.9.1] — Asynchronous Sessions & Cleanups
Added
Non-Blocking DB Interactivity: Implemented a comprehensive asynchronous threading worker pool (session_db_worker) wrapped with GLib's g_thread_new. Heavy MySQL/MariaDB database operations (session creation, listing, cloning, and state deletions) are now offloaded from the main thread, keeping the GUI thread entirely non-blocking and free of lag frames.

Dynamic Help Formatting: Integrated an auto-calculating terminal layout loop into the help command registry. The system now determines the longest command name on compilation and outputs beautifully clean, dot-padded tables dynamically matching any console size.

Changed
Flexible Parser Formatting: Optimized command entry string slicing within the pipeline to allow seamless extraction of commands regardless of whether they are entered natively or preceded by a forward slash (/command).

Fixed
Memory Leak Elimination: Cleaned up transient data allocations inside background session-rendering handlers by strictly running g_free() lifecycles across all transient UUID string arrays and descriptions passed through idle UI threads.

[0.9.0] — Unified Engine Foundations
Added
The Unified Command Engine: Introduced the core CommandRegistry lookup block to commands.c, standardizing string-driven command routing and routing loops via case-insensitive strncasecmp validation protocols.

Advanced Pipeline Tooling: Added full runtime tracking, flag registries, and interface structures for automated context switches:

auto all — Masters the toggle state for core automation pipelines.

autoreply — Controls real-time payload processing and token loops.

autoexe — Safely handles immediate, autonomous shell payload execution gates.

tee — Captures instant raw foreground console buffer records.

noise filter / noise add — Eradicates mitigation noise from active terminal loops.

smart cache — Connects semantic memory tiers locally.

ratelimit / rpm — Implements explicit requests-per-minute threshold limits.

Optimized for Budget/Lite Computations: Structured prompt limits and string buffering rules to support localized development environments using hyper-fast, low-overhead models like gemini-3.1-flash-lite, achieving high reliability without requiring live cloud cloud environments or billing profiles.

* Add files via upload

Added some screen shots and a sql file to create the entire structure of the mysql database this far anyway. There are a lot of bugfixes, and plenty new and updated features since the last version I posted last month. This version is very stable and has been thoroughly tested with System: Provider: Gemini Model: gemini-3.1-flash-lite and gemini-3.0-flash. Some of the new features are: totally reworks help section, session manager, rate limiting, system policies (soon to be a policy manager) . Some bugfixes include: AI pane now scrolls to the end of the output every time - no more having to scroll through all AI output to find the last message, cutdown on the amount of redundant data sent to AI, Fixed blank lines being sent to AI (Gemini use to get real upset that it would send like 30 or so blank lines every time the screen was cleared), Every SQL transaction is not done on the GTK main thread, so no matter the SQL query, it will not make the main window hang for nothing, also asynchronous AI Communications, AI thinking will never hand the terminal window. I'm sure there were plenty more bugfixes and new features that aren't coming to me right now. but that is the overview anyways. Hope you all like the improvements and find it useful! I must say, I've been having fun developing it. I've been using it as my primary terminal emulator now for just about everything. I know it needs a little more polishing but it has come a tremendous way so far! So check it out!!

# aiterm (v0.8.4-alpha)

**The AI-Augmented Terminal Emulator for Power Users with Built-in Zero-Trust Guardrails.**

`aiterm` is a high-performance C/GTK-based terminal emulator that integrates Large Language Models (Gemini/OpenAI) directly into your command-line workflow. It features an asynchronous, thread-safe architecture that captures terminal context, tracks long-term history via MariaDB, and provides automated AI insights with strict, user-configurable security execution boundaries.

---

## 🚀 Key Features

* **Asynchronous AI Pipeline:** Utilizes `GThread` background workers to handle API latency. The terminal remains 100% responsive while the AI processes data.
* **Zero-Trust Policy Guardrails (New):** Intercepts downstream commands matching user-defined policies before execution. Supports hard blocking or interactive human-in-the-loop modal confirmations.
* **Cascading Wildcard Fallback:** Supports a comprehensive catch-all policy (`*`) to safely intercept and gate unknown or untrusted commands that aren't explicitly registered in the database.
* **Atomic Context Capture:** A mutex-locked capture engine gathered via VTE buffer tracking ensures 100% data integrity—no data splicing or duplication, even during high-volume logs or `dmesg` dumps.
* **Persistent SQL History:** Integrated MariaDB backend stores synchronized command execution tables, relevance triggers, and security parameters.
* **Intelligent Automation:** * `tee` mode: Real-time terminal streaming into the active AI context.
    * `autoreply` mode: Automated AI analysis triggered by terminal anomalies and silence.
* **Custom CSS Theming:** High-priority CSS overrides ensure a consistent dark, transparent aesthetic regardless of system-wide theme settings.

---

## ⚙️ Configuration

`aiterm` requires a configuration file to store API keys and database credentials.

1.  **Config Location:** Currently, the application looks for its configuration at `/etc/aiterm.conf`. 
    * *Note: Future versions will migrate this to a user-local `~/.config/aiterm/aiterm.conf`.*
2.  **Setup:** An `aiterm.conf.example` is provided in the root directory.
    ```bash
    # To install the configuration:
    sudo cp aiterm.conf.example /etc/aiterm.conf
    sudo nano /etc/aiterm.conf
    ```
3.  **Required Fields:** Ensure you provide your Gemini/OpenAI API key and your MariaDB login details within this file for the application to initialize.

---

## 🗄️ Database Setup

`aiterm` uses three distinct tables to manage context, intelligence filters, and zero-trust verification. The application will attempt to auto-create these if they do not exist.

### 1. Table: `aiterm_history`
Stores the synchronized stream of terminal output and AI interactions.

CREATE TABLE IF NOT EXISTS aiterm_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    terminal_output LONGTEXT,
    ai_response LONGTEXT,
    is_input TINYINT(1) DEFAULT 0,
    sequence_id INT DEFAULT 0,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);

2. Table: relevance_triggers
Acts as a semantic filter, allowing the application to identify critical terminal events that require AI attention.

CREATE TABLE IF NOT EXISTS relevance_triggers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    trigger_keyword VARCHAR(255) NOT NULL,
    description TEXT,
    is_active TINYINT(1) DEFAULT 1
);

3. Table: command_policy (New)
Powers the zero-trust engine by explicitly mapping commands to their execution thresholds and protection behavior.

CREATE TABLE IF NOT EXISTS command_policy (
    command_name VARCHAR(256) PRIMARY KEY,
    policy_type VARCHAR(32) NOT NULL,
    risk_level INT NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON DUPLICATE KEY UPDATE updated_at=CURRENT_TIMESTAMP
);


🛡️ The Policy Subsystem / Administration
The terminal pane accepts specialized security syntax for real-time adjustments to your command execution boundaries:

Plaintext
/policy <FUNCTION> [ARGUMENTS]
Supported Functions:
/policy ADD <command|*> <ALLOW|BLOCK|APPROVE> <LOW|MEDIUM|HIGH|CRITICAL>

Registers or updates a command rule. Using * defines the Catch-All Wildcard Fallback rule.

/policy DELETE <command>

Removes a verification rule entirely from the database.

/policy LIST

Renders a formatted table of all active rules, modes, and assigned risks directly inside the AI panel.

/policy FIND <command>

Performs a cascading live lookup. It verifies whether an explicit rule governs the command or if the wildcard fallback will capture it.

Enforcement Behavior Toggles:
ALLOW — Command executes instantly downstream without interruption.

BLOCK — The application explicitly denies pipeline processing and logs a policy violation notification.

APPROVE — Intercepts processing and halts execution to trigger an interactive, modal GTK verification window requiring an explicit human click to proceed.

🛠 Engineering Achievements & Fixes in 0.8.4-alpha
Zero-Trust Sub-Command Matrix: Shifted the administrative engine away from flat string tokenizers to a fully isolated database access object layout (policy_dao.c) supporting tiered parameters via stack-allocated sscanf routing.

Cascading Catch-All Mechanics: Designed a two-stage database lookup pipeline (Exact Match → Wildcard Fallback → Safe-Fail Compile-Time Default) protecting the application against unregistered system utility payloads.

Atomic Snapshotting: Resolved "Splicing Bugs" by using GMutex to lock the VTE terminal buffer during text extraction, ensuring the AI never receives fragmented lines.

Dynamic SQL Generation: Overcame the 16KB query limit by implementing a dynamic malloc engine in utils.c that calculates query lengths at runtime, allowing for massive data captures (700KB+).

📦 Build Instructions
Dependencies
Debian/Ubuntu: libgtk-3-dev, libvte-2.91-dev, libmariadb-dev, libcurl4-openssl-dev, libjson-c-dev, libssl-dev.

Compilation
Bash
make clean
make
./aiterm

Command line arguments:
./aiterm --help
Usage: aiterm [OPTIONS]

Options:
  --help			Show this help menu
  --version			Display current version
  --debug			Enable verbose debug logging to stderr
  --features			Show details on features supported by current version
  --master=<key>		Provide the master password directly to decrypt saved config options
  --crypt-pw=<password>		Encrypt a plaintext password to AES-256-CBC encryption for aiterm.conf

Environment Variables:
  AITERM_MASTER_KEY		Fallback variable evaluated if the --master option flag is missing

Examples:
  ./aiterm --master='secretkey' --debug
  export AITERM_MASTER_KEY='secretkey' && ./aiterm

🗺 Roadmap
User-Local Configuration: Moving /etc/aiterm.conf to ~/.config/aiterm/.

Per-Tab Context: Isolating AI history and security scopes for individual terminal tabs.

Local LLM Integration: Supporting Ollama for 100% offline local terminal analysis.

Author: Peter Talbott

Location: Weston, WV

Technical Consultant: Gemini (AI-Human Pair Programming)

This project is a showcase of high-concurrency C programming, enterprise data engineering, and defensive UI design.


-----------------------
# aiterm (v0.8.3-stable)

**The AI-Augmented Terminal Emulator for Power Users.**

`aiterm` is a high-performance C/GTK-based terminal emulator that integrates Large Language Models (Gemini/OpenAI) directly into your command-line workflow. It features an asynchronous, thread-safe architecture that captures terminal context, tracks long-term history via MariaDB, and provides automated AI insights without ever interrupting the terminal’s responsiveness.

---

## 🚀 Key Features

*   **Asynchronous AI Pipeline:** Utilizes `GThread` background workers to handle API latency. The terminal remains 100% responsive while the AI processes data.
*   **Atomic Context Capture:** A mutex-locked capture engine gathered via VTE buffer tracking ensures 100% data integrity—no data splicing or duplication, even during high-volume `dmesg` dumps.
*   **Persistent SQL History:** Integrated MariaDB backend stores synchronized command history and AI interactions.
*   **Intelligent Automation:** 
    *   `tee` mode: Real-time terminal streaming to the AI context.
    *   `autoreply` mode: Automated AI analysis triggered by terminal activity and silence.
*   **Custom CSS Theming:** High-priority CSS overrides ensure a consistent dark, transparent aesthetic regardless of system-wide theme settings.

---

## ⚙️ Configuration

`aiterm` requires a configuration file to store API keys and database credentials.

1.  **Config Location:** Currently, the application looks for its configuration at `/etc/aiterm.conf`. 
    *   *Note: Future versions will migrate this to a user-local `~/.aiterm.conf` or `~/.config/aiterm/aiterm.conf`.*
2.  **Setup:** An `aiterm.conf.example` is provided in the root directory.
    ```bash
    # To install the configuration:
    sudo cp aiterm.conf.example /etc/aiterm.conf
    sudo nano /etc/aiterm.conf
    ```
3.  **Required Fields:** Ensure you provide your Gemini/OpenAI API key and your MariaDB login details within this file for the application to initialize.

---

## 🗄️ Database Setup

`aiterm` uses two tables to manage context and intelligence. The application will attempt to auto-create these if they do not exist.

### 1. Table: `aiterm_history`
Stores the synchronized stream of terminal output and AI interactions. The `sequence_id` is used to maintain chronological integrity during AI context injection.
```sql
CREATE TABLE IF NOT EXISTS aiterm_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    terminal_output LONGTEXT,
    ai_response LONGTEXT,
    is_input TINYINT(1) DEFAULT 0,
    sequence_id INT DEFAULT 0,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### 2. Table: `relevance_triggers`
Acts as a semantic filter, allowing the application to identify critical terminal events that require AI attention.
```sql
CREATE TABLE IF NOT EXISTS relevance_triggers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    trigger_keyword VARCHAR(255) NOT NULL,
    description TEXT,
    is_active TINYINT(1) DEFAULT 1
);
'''

-- Example Seed Data
INSERT INTO relevance_triggers (trigger_keyword, description) VALUES 
('Error', 'General error detection'),
('Success!', 'Validation of successful execution'),
('Segmentation fault', 'Critical memory failure');
```

---

## 🛠 Engineering Achievements & Fixes

*   **Atomic Snapshotting:** Resolved "Splicing Bugs" by using `GMutex` to lock the VTE terminal buffer during text extraction, ensuring the AI never receives fragmented lines.
*   **Dynamic SQL Generation:** Overcame the 16KB query limit by implementing a dynamic `malloc` engine in `utils.c` that calculates query lengths at runtime, allowing for massive data captures (700KB+).
*   **Linked State Machine:** Developed a dependency logic for UI toggles; enabling `autoreply` automatically engages `tee` mode, ensuring the AI always has the necessary context to respond.

---

## 📦 Build Instructions

### Dependencies
*   **Debian/Ubuntu:** `libgtk-3-dev`, `libvte-2.91-dev`, `libmariadb-dev`, `libcurl4-openssl-dev`, `libjson-c-dev`.

### Compilation
```bash
make clean
make
./aiterm
```

---

## 🗺 Roadmap
*   **User-Local Configuration:** Moving `/etc/aiterm.conf` to `~/.config/aiterm/`.
*   **Per-Tab Context:** Isolating AI history for individual terminal tabs.
*   **Local LLM Integration:** Supporting Ollama for 100% offline terminal analysis.

---

**Author:** Peter Talbott  
**Technical Consultant:** Gemini (AI-Human Pair Programming)  
*This project is a showcase of high-concurrency C programming and AI integration.*

---
