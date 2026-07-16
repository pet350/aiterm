Markdown
# aiterm — The AI-Augmented Terminal Emulator
# Version 0.9.5-omega

[![C Version](https://img.shields.io/badge/Language-C99-00599C?logo=c&logoColor=white)](https://gcc.gnu.org/)
[![GTK Version](https://img.shields.io/badge/UI-GTK%203.0-734F96?logo=gtk&logoColor=white)](https://www.gtk.org/)
[![Security](https://img.shields.io/badge/Encryption-AES--256--CBC-green?logo=openssl&logoColor=white)](https://www.openssl.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

`aiterm` is an advanced, multi-tabbed terminal emulator written in C, leveraging GTK 3 and the VTE library to inject an interactive, context-aware AI pane directly alongside your shell workflows. Built with defensive engineering principles, it monitors running contexts, handles command telemetry safely, and securely parses terminal and session activities through modern AI models (including Google Gemini and OpenAI) without breaking command-line isolation.

---

## 🚀 Key Features

* **Dual-Pane AI Architecture:** Run normal terminal commands on the left while interacting with a dedicated AI developer companion on the right.
* **Dynamic API Keyring (v0.9.6):** Store, read, and manage multiple AI provider keys and models from an encrypted file on-disk (`~/.config/aiterm/keyring.enc`). Key targets are loaded dynamically using an ephemeral memory layout, keeping keys strictly decrypted in RAM and completely isolated.
* **Persistent Session Pinning:** Maintain state across restarts. Dynamic, multi-threaded MariaDB/MySQL hooks auto-save session histories under strict, unique UUID schemas.
* **Robust Smart Caching:** Leverages Gemini's API context-caching layers to handle enormous, multi-megabyte terminal outputs sustainably, with on-the-fly threshold checking and safety-valve cache invalidations.
* **Active Noise Filtering:** Eliminate terminal output noise (such as high-frequency shell redraws, prompts, and common logs) from telemetry pipelines using an integrated GtkTree-based manager.
* **Encrypted Configuration & Security:** Sensitive database credentials, environment variables, and API keys are stored encrypted via OpenSSL's AES-256-CBC engine. Your keys stay safe, unlocked globally at runtime via CLI parameters or environment flags.
* **Rate-Limit Safeguards:** Real-time Requests-Per-Minute (RPM) enforcement guarantees you won't trigger throttling issues or API-blocking limits on developer tiers.

---

## 🛠 Prerequisites & Dependencies

To build `aiterm` from source, ensure your Linux development environment has the following libraries installed:

```bash
# Debian / Ubuntu / OpenSUSE equivalents
sudo apt-get install \
    libgtk-3-dev \
    libvte-2.91-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libmariadb-dev \
    libjson-c-dev
Database Integration
aiterm relies on a MariaDB/MySQL backend to track dynamic shell context, persistent session layouts, and past interactions.

Import the provided schema:

Bash
mysql -u root -p < schema.sql
Set up your user credentials inside aiterm.conf (which will be encrypted securely using the master configuration workflow).

🏗 Installation & Build
Compile the project cleanly using the optimized Makefile:

Bash
# Clean previous build pipelines and build the binary
make clean
make -j$(nproc)
To run stress tests safely without false positives on GTK's default slice allocation pools, use:

Bash
G_DEBUG=gc-friendly G_SLICE=always-malloc valgrind --leak-check=full --show-leak-kinds=definite ./aiterm
🔒 Security Configuration
To safeguard your keys, aiterm expects an AES-256-CBC Master Password. You can supply this master key directly or set it as a temporary shell variable:

Bash
# Option A: Command Line parameter
./aiterm --master="YourSecureMasterKey"

# Option B: Environment variable (Fallback option)
export AITERM_MASTER_KEY="YourSecureMasterKey"
./aiterm
Encrypting Plaintext Credentials
To securely encode plain-text connection passwords or API keys for your configuration files, use the integrated encryption tool:

Bash
./aiterm --master="YourSecureMasterKey" --crypt-pw="MySecretDatabasePassword"
Copy the outputted AES-256-CBC cipher block directly into your aiterm.conf configuration structure.

🎛 Command Line Options
Option	Description
--help	Show the application command line help menu.
--version	Display current running version, build ID, and build compilation time.
--debug	Enable verbose, real-time logging output to stderr.
--features	Show detailed architectural features implemented in the current build.
--master=<key>	Directly provide the master key used to decrypt configurations at startup.
--list-models	Query and list available API models matching the current provider context.
--crypt-pw=<text>	Encrypt plaintext properties to AES-256-CBC formatting using the active master key.
💬 Built-in Slash Commands
You can execute control mechanics on-the-fly directly inside the AI input terminal window by prepending standard slash commands:

Session Management
/session new — Spawn a clean, distinct AI conversation session.

/session list — Query the database to list historical sessions.

/session load <UUID> — Load a chosen session context directly.

/session default <UUID> — Set a specific session UUID to auto-load on start.

/open session manager — Open the GTK session management graphical utility.

Operational & Telemetry Control
/tee — Toggle immediate real-time terminal stdout capturing (on/off).

/autoreply — Toggle automatic analysis of prompt execution outputs (on/off).

/smart cache — Toggle the Gemini smart caching layer dynamically (on/off).

/xml tagging — Enable or disable clean XML payload boundaries on model queries (on/off).

/rpm <limit> — Adjust the Rate Limiter ceiling on-the-fly.

/open noise manager — Open the Active Noise Filter profile panel to adjust shell regex and patterns.

🛠 Roadmap to v0.9.6: The API Keyring Engine
In 0.9.6, we are migrating from isolated configuration files to an active, encrypted API Keyring Module. This allows developers to seamlessly pivot across API nodes and service tiers (e.g., from local mock testing, to Gemini Flash, to full-featured OpenAI models) with instant configuration loading and active thread-safety handshakes:

C
// Ephemeral structural footprint of a Keyring Entry inside memory
typedef struct {
    char *profile_name;     // e.g., "Gemini-Flash-Fast"
    char *provider;         // e.g., "gemini"
    char *model;            // e.g., "gemini-1.5-flash"
    char *api_key;          // Ephemeral key (only decrypted in memory)
    char *base_url;
    char *endpoint;
    ProviderKind kind;      // Enum-based parser hook
} KeyringEntry;
When switching profiles, aiterm automatically frees stale structures, safely resets running model parameters, invalidates active smart cache blocks to prevent parameter leakages, and scales token constraints dynamically to protect thread safety.


***

### Pro-Tip on Your GitHub Release:
When you finalize the release notes on the GitHub Tags UI, you can link directly to this updated markdown or make a dedicated release branch so your users can see the secure database and crypto steps easily.

This [Readme Template for GitHub Repos](https://www.youtube.com/watch?v=eVGEea7adDM) video provides a great breakdown of essential markdown structures, badge implementations, and layout guidelines that will help make your repository's front page look clean and professional.


http://googleusercontent.com/youtube_content/0

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


Features a completly reworked now totally modular AppContext. This was a long time coming,
completely composed of sub-structures with no individual variables:

typedef struct {
    SQL_DataBase database;		 // Sub-Structure for mysql database access
    RunTimeVariables aiterm_runtime;     // Sub-Structure for misc runtime variables
    ResourceControl access;		 // Sub-Structure for control over resources
    SystemBooleans sys;			 // Sub-Structure for all system control booleans
    UIComponents ui;			 // Sub-Structure for GtkWidgets of all UI components
    RateLimiter limiter;                 // Sub-Structure for Rate Limiter variables
    NoiseFilter noise;	 		 // Sub-Structure for all Noise filter variables
    SessionContext session;		 // Sub-Structure for the current sessions variables
    ProviderConfig provider_config;	 // Sub-Structure for AI Provider config variables
    APIKeyring keyring;                  // <-- NEW: API Keyring Sub-Structure (Added 0.9.6)
    ManagerWindows manager;		 // Sub-Structure for GtkWidgest of all the manager windows
    LocalCommand local;	  		 // Sub-Structure for local commands being cached.
    TokenTracker tokens;		 // Sub-Structure for keeping track of AI Tokens
    GeminiCacheState gemini_cache;	 // Sub-Structure for Smart Cache variables
    TagPayload xml;			 // Sub-Structure for handling xml taggs
    SysWidgets gui;			 // Sub-Structure for handling system GUI Widgets
    SecurityConfig security;		 // Sub-Structure for security keys
} AppContext;
The whole backbone of the app, now very clean and organized. It took a week+ to perform the whole 
overhaul on this main application structure, making sure that no function was adversly affected. 
It was times taking effort But in the end very worth it, it is now rock solid, very fast,
and has passed every stress test thrown at it with flying colors!

- AI Never misses a byte from the terminal 
- reworked rate limiter now works 100% with adjustable requests per minute
- reworked noise filter manager makes it easy to add/delete noise patterns
- reworked XML tagging. now all the content sent to AI is XML tagged
    - Dramaticly reduced the token useage per request
    - Speeded up response time
    - AI now gets focused on the task not some other random thing found in history
- reworked history manager makes it easy to see the history being sent and delete enteries that are unwanted



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
