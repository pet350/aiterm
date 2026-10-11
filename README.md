# aiterm

## The AI-Augmented Terminal Emulator

**aiterm** is a Linux desktop terminal emulator written in C using GTK 3
and VTE, with a dedicated AI pane alongside the terminal. It combines
normal shell work, persistent MariaDB-backed sessions, AI-assisted
terminal analysis, optional automatic command execution, SNMP telemetry,
a provider abstraction, caching, noise filtering, rate limiting,
export/printing, and session-aware configuration.

> **Current source tree:** 0.9.12-gamma / October 2026\
> **Primary build system:** GNU Make\
> **Configuration file:** searched in several locations, falling back to
> `/etc/aiterm.conf` (see [Configuration](#configuration))

The project is built around one principle:

> The terminal remains a normal terminal. AI assistance is an additional
> layer that can observe selected terminal activity, analyze it,
> maintain context, and optionally act on explicitly enabled workflows.

------------------------------------------------------------------------

## What's New in 0.9.12

### 0.9.12-gamma

- **Per-tab Tee capture.** Terminal capture now works in every open tab,
  not just the newest one. Each VTE keeps its own row counters and its
  own 250 ms watcher timer. Captures are stamped `[tab pts/N]`, and a
  manual AI question uses only the focused tab's text (see
  [Tee Capture and Tabs](#tee-capture-and-tabs)).
- Provider-specific encrypted API keys (`OPENAI_KEY`, `GEMINI_KEY`, ...)
  and an **API Keys** reference window (`Tools → Open api keys window`).
- `--provider` and `--bw` command-line options.
- Config file lookup across several locations, plus the `AITERM_CONFIG`
  environment variable.
- Environment overrides for provider, model, and transport settings.
- `make install` / `make uninstall` targets and SQL-script-driven schema
  initialization.

### 0.9.12-alpha / beta

- **AutoChunk** reworked around a provider-aware single-request context
  budget (see [AutoChunk and Context Budget](#autochunk-and-context-budget)).
- History loading obeys the same request budget instead of always loading
  up to 100 records.
- OpenAI-compatible and Gemini requests no longer append every terminal
  chunk into one oversized request. Large captures stay preserved in
  MariaDB; only the bounded portion is sent in the current stateless API
  request.
- Final serialized JSON payload diagnostics for troubleshooting provider
  limits.
- Fresh terminal output is harvested before a manual AI request, so
  output that arrived between Tee timer cycles is available immediately.
- API conversation history includes only non-TEE `user` and `assistant`
  rows and excludes `API_ERROR:` responses, so terminal/SNMP telemetry
  and provider failures cannot masquerade as conversation turns.
- Opening the Provider Manager synchronizes pending terminal output and
  persists the Tee accumulator without launching an AI request.
- Fixed a NULL dereference and an early-return allocation leak in
  AutoChunk.

------------------------------------------------------------------------

## Table of Contents

1.  [Architecture at a Glance](#architecture-at-a-glance)
2.  [Major Features](#major-features)
3.  [System Requirements](#system-requirements)
4.  [Build Dependencies](#build-dependencies)
5.  [Building from Source](#building-from-source)
6.  [Installing](#installing)
7.  [Configuration](#configuration)
8.  [Encrypted Credentials](#encrypted-credentials)
9.  [AI Providers](#ai-providers)
10. [Provider Manager and API Keys Window](#provider-manager-and-api-keys-window)
11. [AutoChunk and Context Budget](#autochunk-and-context-budget)
12. [MariaDB Database](#mariadb-database)
13. [Terminal and AI Workflow](#terminal-and-ai-workflow)
14. [Tee Capture and Tabs](#tee-capture-and-tabs)
15. [Menus](#menus)
16. [System Toggles](#system-toggles)
17. [Idle Suspension](#idle-suspension)
18. [Automatic Execution and Policies](#automatic-execution-and-policies)
19. [Noise Filtering](#noise-filtering)
20. [Smart Cache](#smart-cache)
21. [Rate Limiting](#rate-limiting)
22. [AI Retry](#ai-retry)
23. [SNMP Monitoring](#snmp-monitoring)
24. [Sessions and Persistent Context](#sessions-and-persistent-context)
25. [History Manager](#history-manager)
26. [Exporting Sessions](#exporting-sessions)
27. [Printing](#printing)
28. [Multi-Tab Terminal](#multi-tab-terminal)
29. [Other GUI Features](#other-gui-features)
30. [XML Payload Tagging](#xml-payload-tagging)
31. [Debugging](#debugging)
32. [Command Reference](#command-reference)
33. [Command-Line Options](#command-line-options)
34. [Environment Variables](#environment-variables)
35. [Configuration Reference](#configuration-reference)
36. [Troubleshooting](#troubleshooting)
37. [Development Notes](#development-notes)
38. [Project File Map](#project-file-map)
39. [Security Notes](#security-notes)
40. [Known Limitations and Source-Tree Caveats](#known-limitations-and-source-tree-caveats)
41. [Recommended First-Run Procedure](#recommended-first-run-procedure)
42. [License](#license)

------------------------------------------------------------------------

# Architecture at a Glance

``` text
                    +----------------------+
                    |      aiterm GUI      |
                    |       GTK 3          |
                    +----------+-----------+
                               |
                 +-------------+-------------+
                 |                           |
        +--------v--------+         +--------v--------+
        | Terminal / VTE  |         |     AI Pane     |
        | Multi-tab shell |         | GTK Text View   |
        +--------+--------+         +--------+--------+
                 |                           |
        +--------v---------------------------v--------+
        |            Application Context              |
        |                 AppContext                  |
        +----+-----------+-----------+-----------+----+
             |           |           |           |
        +----v---+   +---v----+  +---v----+  +---v----+
        |  Tee   |   | Session|  |  SNMP  |  | Auto   |
        | Engine |   | / DB   |  | Poller |  | Exec   |
        +----+---+   +---+----+  +---+----+  +---+----+
             |           |           |           |
             +-----------+-----------+-----------+
                         |
                   +-----v------+
                   | AutoChunk  |
                   | (budget)   |
                   +-----+------+
                         |
                   +-----v------+
                   | AI Provider|
                   | Abstraction|
                   +-----+------+
                         |
             +-----------+-----------+
             |                       |
       +-----v------+          +-----v------+
       |   Gemini   |          | OpenAI-    |
       | generate   |          | compatible |
       | Content    |          | providers  |
       +------------+          +------------+
```

The provider abstraction distinguishes Gemini's `generateContent`
protocol from OpenAI-compatible Chat Completions providers.

GTK is event-driven, and aiterm uses GTK/GLib timers, signals, and event
handlers for the GUI, the per-tab Tee watchers, the SNMP ticker, the idle
watchdog, and other asynchronous operations. All AI and SQL transactions
run on worker threads and report back to the GTK main thread.

------------------------------------------------------------------------

# Major Features

## Dual-pane interface

-   A VTE-backed terminal pane with multi-tab support.
-   An AI conversation/output pane with an input line.
-   A token/status area.
-   An optional SNMP ticker.

The terminal remains a normal shell while AI features are selectively
enabled.

## AI provider abstraction

The request layer is centralized in `ai_provider.c`. Built-in provider
names:

``` text
openai   gemini   groq   openrouter   mistral   ollama   custom
```

Gemini uses its own `generateContent` protocol. All other providers use
the OpenAI-compatible Chat Completions path. Each provider has its own
encrypted API key.

## Persistent sessions

Sessions use UUIDs and are stored in MariaDB. Sessions can be created,
listed, loaded, described, deleted, marked as the default session, and
used to restore selected configuration values.

## Terminal capture / Tee

Tee captures terminal activity for AI analysis and for the database
history. It works with Auto Reply so captured output can be analyzed
automatically. Capture runs per tab.

## Automatic execution

AI-generated commands can be placed in an execution queue. A policy
system (ALLOW / BLOCK / APPROVE per command) and confirmation dialogs
govern what actually runs.

## SNMP telemetry

Polls configured targets and OIDs, shows a scrolling ticker, optionally
sends telemetry to the AI, and provides a GUI and command interface for
managing targets.

## AutoChunk

Provider-aware context budgeting keeps each stateless API request within
a safe size while the full capture history stays in MariaDB.

## Smart Cache, noise filtering, rate limiting, AI Retry

Described in their own sections below.

## Export and printing

Export the active session as Plain Text, HTML, JSON, or XML. A GTK print
workflow is also provided.

## Encrypted credentials

Provider API keys and the MariaDB password are encrypted in the config
file with AES-256-CBC (OpenSSL).

------------------------------------------------------------------------

# System Requirements

Linux only. A graphical session is required (aiterm is a GTK desktop
application). The program also requires at least one non-loopback IPv4
network interface at startup; otherwise it exits (see
[Known Limitations](#known-limitations-and-source-tree-caveats)).

Required to build:

-   GCC or a compatible C compiler, GNU Make, `pkg-config`
-   GTK 3, VTE 2.91, GLib 2.x
-   libcurl, json-c
-   MariaDB/MySQL client development libraries
-   OpenSSL (libcrypto), libuuid, Net-SNMP
-   `glib-compile-resources` (GLib tools)
-   `uuidgen` (used to generate the build ID)

A running MariaDB/MySQL server is needed for sessions, history, noise
filters, policies, and SNMP targets.

------------------------------------------------------------------------

# Build Dependencies

## Debian / Ubuntu

``` bash
sudo apt update

sudo apt install \
    build-essential \
    pkg-config \
    uuid-runtime \
    uuid-dev \
    libgtk-3-dev \
    libvte-2.91-dev \
    libglib2.0-dev \
    libcurl4-openssl-dev \
    libjson-c-dev \
    libssl-dev \
    libmariadb-dev \
    libmariadb-dev-compat \
    libsnmp-dev
```

The Makefile auto-detects the MariaDB pkg-config module
(`libmariadb`, then `mariadb`, then `mysqlclient`). Check what your
system provides:

``` bash
pkg-config --list-all | grep -Ei 'mariadb|mysql'
```

Verify the main dependencies:

``` bash
pkg-config --modversion gtk+-3.0
pkg-config --modversion glib-2.0
pkg-config --modversion vte-2.91
pkg-config --modversion libcurl
pkg-config --modversion json-c
```

The Makefile links with: `pthread curl crypto uuid netsnmp`.

## openSUSE

Install the development packages for: `gtk3`, `vte`, `glib2`, `libcurl`,
`json-c`, `mariadb-connector-c`, `openssl`, `libuuid`, `net-snmp`,
`pkg-config`, `make`, `gcc`. This source tree was most recently built and
tested on openSUSE.

The code includes `<mariadb/mysql.h>` and the Makefile may add
`-I/usr/include/mysql/` on some distributions. If `mysql.h` cannot be
found, confirm the MariaDB Connector/C development package is installed.

------------------------------------------------------------------------

# Building from Source

``` bash
cd /path/to/aiterm
make
```

Clean rebuild:

``` bash
make clean
make -j"$(nproc)"
```

`make clean` removes the objects, the `aiterm` binary, the generated
`resources.c`, and the generated `build_id.h`.

The build generates:

-   `resources.c`, from `resources.xml` and `aiterm-icon.png`, using
    `glib-compile-resources`.
-   `build_id.h`, containing a UUID build ID (`uuidgen`) and a build
    timestamp.

If `uuidgen` is missing on Debian/Ubuntu: `sudo apt install uuid-runtime`.

> **Clock skew:** if `make` warns that a file has "a modification time in
> the future", your system clock is behind the file timestamps (common in
> VMs). Run `make clean && make`, or `touch *.c *.h`, or fix the clock.

## Verify the build

``` bash
./aiterm --version
./aiterm --features
./aiterm --help
./aiterm --provider
```

------------------------------------------------------------------------

# Installing

The Makefile provides install targets:

``` bash
sudo make install
```

| Target            | Action                                                       |
|-------------------|--------------------------------------------------------------|
| `make install`    | Builds, then runs `install-bin` and `install-db`             |
| `make install-bin`| Installs `aiterm` to `$(DESTDIR)$(PREFIX)/bin` (mode 0755)   |
| `make install-db` | Installs `db/*.sql` to `$(DESTDIR)$(DATADIR)/aiterm/db`      |
| `make uninstall`  | Removes only what install placed                             |

Defaults: `PREFIX=/usr/local`, `DATADIR=/usr/share`, so the binary goes to
`/usr/local/bin/aiterm` and the SQL scripts to `/usr/share/aiterm/db`.

``` bash
make install PREFIX=/opt/aiterm            # relocate the binary
make install DATADIR=/opt/aiterm/share     # relocate the SQL scripts
make install DESTDIR=/tmp/stage            # staging root for packaging
```

`DESTDIR` is used only by the install/uninstall recipes. It is *not*
compiled into the binary. The final `SQLDIR` is baked into the binary as
`AITERM_SQL_DIR_DEFAULT`, so if you override `DATADIR` you must build with
the same value. You can also override the location at run time with
`AITERM_SQL_DIR`.

> **Note:** `install-db` copies `db/*.sql`. If there is no `db/` directory
> it prints a warning and skips the schema install. The source archive
> reviewed for this README does not contain a `db/` directory (see
> [MariaDB Database](#mariadb-database)).

For development you can also run `./aiterm` directly from the build
directory.

------------------------------------------------------------------------

# Configuration

## Config file location

At startup aiterm looks for its configuration file in this order and uses
the first one that exists:

1.  The path in the `AITERM_CONFIG` environment variable
2.  `/etc/.<UID>.aiterm.conf` (per-user file in `/etc`, where `<UID>` is
    the numeric user id)
3.  `~/.config/aiterm.conf`
4.  `~/.aiterm.conf`
5.  `/etc/aiterm.conf` (default if nothing else is found)

Run with `--debug` to see which file was chosen.

When aiterm runs as root it forces `HOME=/root` and related XDG variables
to `/root/...` before the lookup.

The repository contains `aiterm.conf.example`, a minimal starting file.
The running application's `save_config()` writes the full format.

## Important configuration behavior

Saved configuration begins with:

``` text
# WARNING: Any changes made to this file will be overwritten
```

-   Manual edits can be overwritten whenever the app saves its config
    (for example `Configuration → Save to Conf File` or `/save config`).
-   Prefer the Provider Manager and the toggle menus where practical.
-   Back up the file before major changes.
-   `save_config()` rewrites the file in its own format; comments you add
    are lost.
-   `save_config()` does not write the legacy `api_key=` line any more.

## Creating the initial configuration

``` bash
cp aiterm.conf.example ~/.config/aiterm.conf     # or /etc/aiterm.conf
chmod 600 ~/.config/aiterm.conf
```

Then edit the values:

``` ini
provider=gemini
model=gemini-flash-latest

GEMINI_KEY=<encrypted Gemini key>

db_host=127.0.0.1
db_user=aiterm
db_pass=<encrypted DB password>
db_name=aiterm_db
```

Do **not** put plaintext API keys or database passwords in the file. The
loader expects encrypted values.

If `provider=` is absent from the file, the loader defaults to `openai`.

------------------------------------------------------------------------

# Encrypted Credentials

aiterm encrypts, using AES-256-CBC with a master key:

-   Each provider's API key (`OPENAI_KEY`, `GEMINI_KEY`, `GROQ_KEY`,
    `OPENROUTER_KEY`, `MISTRAL_KEY`, `OLLAMA_KEY`, `CUSTOM_KEY`).
-   The MariaDB password (`db_pass`).

## Supply the master key

``` bash
./aiterm --master='YOUR_MASTER_KEY'
```

or

``` bash
export AITERM_MASTER_KEY='YOUR_MASTER_KEY'
./aiterm
```

If neither is supplied, the application prompts for it
(`Enter Master Encryption Key:`).

## Encrypt a key or password

``` bash
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='PLAINTEXT_SECRET'
```

The program prints the encrypted string. Put it in the matching field:

``` ini
GEMINI_KEY=ENCRYPTED_VALUE
db_pass=ENCRYPTED_VALUE
```

## Inspect decrypted credentials

``` bash
./aiterm --master='YOUR_MASTER_KEY' --decrypt-pw
```

This prints the decrypted value of every provider key
(`OPENAI_KEY` ... `CUSTOM_KEY`) and the database password.

**Use this command carefully.** The output is plaintext. Do not redirect
it to logs, shell history, screenshots, or shared terminal recordings.

## Legacy single-key configs

Older 0.9.x files with a single `api_key=` line are still accepted. The
value is migrated into the configured provider's key slot after the file
is parsed. New configurations should use the provider-specific keys.

------------------------------------------------------------------------

# AI Providers

Implemented in `ai_provider.c`, `gemini.c`, `openai.c`, and
`provider_manager_gui.c`.

| Provider     | Protocol                  | Default base URL                                 | Default model                |
|--------------|---------------------------|--------------------------------------------------|------------------------------|
| `gemini`     | Gemini `generateContent`  | `https://generativelanguage.googleapis.com/v1beta` | `gemini-flash-latest`      |
| `openai`     | OpenAI Chat Completions   | `https://api.openai.com/v1`                      | `gpt-5-nano`                 |
| `groq`       | OpenAI-compatible         | `https://api.groq.com/openai/v1`                 | `openai/gpt-oss-120b`        |
| `openrouter` | OpenAI-compatible         | `https://openrouter.ai/api/v1`                   | `openai/gpt-oss-20b:free`    |
| `mistral`    | OpenAI-compatible         | `https://api.mistral.ai/v1`                      | `mistral-small-latest`       |
| `ollama`     | OpenAI-compatible (local) | `http://127.0.0.1:11434/v1`                      | `llama3.2`                   |
| `custom`     | OpenAI-compatible         | OpenAI defaults, overridden by saved/ENV values  | as configured                |

Notes:

-   Gemini supplies its API key as a query parameter (`key`) against
    `models/<model>:generateContent`.
-   OpenAI-compatible providers use `chat/completions` with an
    `Authorization: Bearer <key>` header.
-   Ollama sends no auth header (no API key required).
-   Values saved by the Provider Manager (base URL, endpoint, auth header
    and scheme, query key, key-in-query flag) override these defaults.
-   Each provider uses only its own key. aiterm does **not** fall back
    from one provider's credential to another's.

------------------------------------------------------------------------

# Provider Manager and API Keys Window

## Provider Manager

Open `Managers → AI Provider Manager`, or:

``` text
/open provider manager
```

It configures: provider name, model, base URL, endpoint, authentication
header and scheme, API key, query-string key name, whether the key is sent
in the query, and the protocol. Built-in templates fill sensible
defaults.

Opening the manager first captures pending terminal output and persists
the Tee accumulator (without sending anything to the AI).

### Test the provider

The manager includes a test operation that sends a small request and
reports whether the provider returned assistant text. It is a quick check
of connectivity, endpoint, model name, key, and protocol.

## API Keys window

`Tools → Open api keys window`, or:

``` text
/open provider api keys
```

Shows a reference window of provider key-signup links (implemented in
`provider_keys.c`). **Key values are never displayed** in this window.

------------------------------------------------------------------------

# AutoChunk and Context Budget

`auto_chunk.c` / `auto_chunk.h` keep each stateless API request inside a
provider-appropriate size.

**Per-request context budgets** (bytes of context, chosen by provider):

| Provider                     | Budget  |
|------------------------------|---------|
| Gemini (or Gemini protocol)  | 32768   |
| OpenAI                       | 16000   |
| Groq                         | 6000    |
| Everything else              | 16000   |

**How the budget is split:**

-   The prompt plus about 1 KB of fixed overhead is subtracted first.
-   Terminal/TEE context may use at most 3/4 of what remains.
-   History gets the remainder, capped at 12000 bytes. Unused terminal
    allowance is given back to history (still capped at 12000).
-   Large captures are never lost: the full capture stays in MariaDB; only
    the bounded portion goes into the current request.

**Chunking defaults:** maximum chunk size 32768 bytes, overlap 1024
bytes. Chunk boundaries are UTF-8-safe and prefer natural breaks.

Debug mode (`--debug`) prints the budget calculation and the final
serialized JSON payload size for troubleshooting provider limits.

------------------------------------------------------------------------

# MariaDB Database

aiterm uses the MariaDB/MySQL C client and keeps **one** global
connection (guarded by a mutex) shared by all subsystems.

## What the application creates

At startup, on a background thread, aiterm:

1.  Connects to `db_host` as `db_user` (with `CLIENT_MULTI_STATEMENTS`).
2.  Runs `CREATE DATABASE IF NOT EXISTS <db_name>` and selects it.
3.  Executes every `*.sql` file in the SQL directory, in filename order
    (`001_...sql`, `002_...sql`, ...). It stops at the first failing
    script.

## Where the SQL directory is

-   `AITERM_SQL_DIR` environment variable, if set and it exists;
    otherwise
-   `AITERM_SQL_DIR_DEFAULT`, normally `/usr/share/aiterm/db`
    (see [Installing](#installing)).

## Tables used by the code

The C source contains **no** `CREATE TABLE` statements. All tables must be
created by the SQL scripts:

| Table                | Used for                                           |
|----------------------|----------------------------------------------------|
| `aiterm_history`     | Conversation turns, TEE captures, SNMP rows        |
| `sessions`           | Session UUIDs, descriptions, saved toggle states   |
| `snmp_targets`       | SNMP target definitions                            |
| `noise_filters`      | Noise filter patterns                              |
| `command_policy`     | Auto-execute policies (`command_name`, `policy_type`, `risk_level`) |
| `relevance_triggers` | Relevance trigger data                             |

`aiterm_history` rows include at least: role, content, tee flag
(`is_tee`), session UUID, sequence id, and a creation timestamp. Roles
used include `user`, `assistant`, `terminal`, and `snmp`.

> **Important:** the source archive reviewed for this README does **not**
> include a `db/` directory or any `.sql` files. A fresh deployment needs
> schema scripts that create the tables above with columns compatible with
> the queries in the source. Without them, session, history, noise,
> policy, and SNMP features will fail.

## Database permissions

The configured account must be able to connect, create/select the
database (or you create it manually), run the schema scripts, and
insert/update/delete/select rows in the application tables. If the account
cannot create databases, create `aiterm_db` yourself and grant access:

``` sql
CREATE DATABASE aiterm_db;
GRANT ALL PRIVILEGES ON aiterm_db.* TO 'aiterm'@'%' IDENTIFIED BY '...';
```

## Connection behavior

Operations are mutex-protected. If the connection drops, run:

``` text
/reset db
```

and check the debug output.

------------------------------------------------------------------------

# Terminal and AI Workflow

``` text
Shell command
     |
     v
VTE terminal (one per tab)
     |
     +---- normal terminal use
     |
     +---- Tee capture, if enabled
                |
                v
        Noise filtering
                |
                v
        Context / session logic
                |
                v
          AutoChunk budget
                |
                v
           AI provider
                |
                v
             AI pane
```

You can type to the AI pane input while independently using the terminal.

## Tee mode

``` text
/tee on
/tee off
/tee status
```

or use the Toggles menu. `Tools → Flush Tee Buffer` forces a flush.

## Auto Reply

Auto Reply enables real-time analysis of captured terminal output:

``` text
/autoreply on
/autoreply off
/autoreply status
```

## Tee and Auto Reply together

Tee and Auto Reply are separate functions:

-   **Tee ON, Auto Reply OFF:** terminal data is captured and saved to
    the database only.
-   **Tee ON, Auto Reply ON:** captured data is saved *and* sent to the
    AI for analysis.

In the GUI, turning Tee off also turns Auto Reply off, and turning Auto
Reply on also turns Tee on.

------------------------------------------------------------------------

# Tee Capture and Tabs

Capture is **per tab** as of 0.9.12-gamma.

## How it works

-   Every terminal tab keeps its own capture state: last cursor row/column,
    last processed row, and a silence counter.
-   Every tab has its own timer (250 ms). After about 2 seconds with no
    cursor movement in that tab (8 ticks), its new output is appended to
    the shared accumulator and flushed.
-   A tab's timer stops automatically when the tab is closed.
-   Each capture is stamped with its source tab, e.g.:

    ``` text
    [tab pts/3]
    <captured text>
    ```

    The `pts/N` name matches the tab label.
-   The accumulator is capped at 64 KB (oldest data is dropped).
-   Flushed captures are saved to `aiterm_history` as `terminal` rows
    (tee flag set). With Auto Reply on, they are also sent to the AI.

## When you ask the AI a question

1.  Output not yet harvested from the **focused tab** is captured first.
2.  If the accumulator has pending data, that is used as terminal context
    (source: pending-tee).
3.  Otherwise the newest saved TEE row **for the focused tab** is used
    (source: latest-db-tee). Rows are matched by their `[tab pts/N]`
    stamp, so another tab's newest row is never substituted.

If the focused tab has nothing saved, no TEE row is attached.

## Things to know

-   `--debug` output goes to stderr. If you start aiterm from a terminal
    that is itself a tab inside another aiterm instance, that tab will
    contain aiterm's own debug log and capturing it will send that log to
    the AI.
-   TEE rows saved before the `[tab ...]` stamp existed carry no tab tag
    and will not be found by the per-tab lookup. Use fresh output when
    testing.
-   Background-tab captures are saved under whichever session UUID is
    current when they flush (see
    [Known Limitations](#known-limitations-and-source-tree-caveats)).

------------------------------------------------------------------------

# Menus

## File
System Status, Clear AI History, Print…, Export Session…, Exit

## Edit
Copy Terminal, Paste Terminal

## Managers
Session Manager, History Manager, Noise Filter Manager, Policy Manager,
SNMP Manager, AI Provider Manager

## Tools
Flush Tee Buffer, Open api keys window, Preferences

## AI Controls
-   **System & Help:** Standard Help, Extended Help, Operational Status,
    Feature Changelog, Hardware Analysis, Database History Logs,
    Application Version, Clear AI Response Pane, Force Reset AI Async
    States
-   **Configuration:** Show Active Provider Info, Query Available Remote
    Models, Reload from Conf File, Save to Conf File, Configure Requests
    Per Minute (RPM), Force Re-establish Database Sockets
-   **Toggles:** see [System Toggles](#system-toggles)
-   **Sessions:** session commands (new, list, show, load, description,
    default, no default, delete, read/write global)
-   **Noise Filters:** List, Add, Delete
-   **AI Retry Options:** Enable Auto-Retry, Set Max Retry Attempts…, Set
    Retry Delay (Sec)…

## Help
Help Content, About

Every menu action that runs a local command goes through the same command
registry as the AI input line.

------------------------------------------------------------------------

# System Toggles

`AI Controls → Toggles` contains:

| Toggle                                       | Purpose                                              |
|----------------------------------------------|------------------------------------------------------|
| Real-Time Prompt Analysis                    | Auto Reply: analyze captured terminal output         |
| AI Payload Auto-Execution                    | Allow AI command payloads into the execution queue   |
| Immediate Terminal Capturing (Tee)           | Capture terminal output                              |
| Debug mode                                   | Verbose diagnostics on stderr                        |
| XML Payload Tagging                          | Wrap payload sections in XML-style tags              |
| Mitigation Noise Filters                     | Apply configured noise-filter patterns               |
| Semantic Local Caching                       | Smart Cache                                          |
| Active Rate Limiting Protection              | Requests-per-minute limiter                          |
| Read from global database                    | Allow reading the global history/context             |
| Write to global database                     | Write context to the global stream                   |
| Send SNMP Payload to AI                      | Allow SNMP telemetry to reach the AI                 |
| SNMP Ticker                                  | Scrolling SNMP display                               |
| AI retry                                     | Automatic retry on failure                           |
| Session config                               | Load saved settings per session                      |

`/auto all on|off|status` toggles Tee, Auto Reply, and Auto Execute
together. It exists as a command; the Toggles menu has no separate
"Auto All" item.

------------------------------------------------------------------------

# Idle Suspension

Implemented in `idle.c` / `idle.h`. It stops unattended runtime features
from running indefinitely.

After the configured period without user activity, aiterm:

1.  Saves the state of the managed toggles.
2.  Turns them off and updates the GUI check items.
3.  Stops the SNMP ticker timer if it was active.
4.  Keeps the application running.
5.  Does not persist the temporary idle-off state as your configuration
    (the state is restored before the final database sync at exit).

When activity resumes, the prior toggle states are restored and the
ticker timer is recreated if it had been on.

## Configure the timeout

``` ini
idle_timeout_minutes=10
```

The loader accepts 0 through 1440. In the current code a value of **0 is
replaced by the default of 10 minutes**, so 0 does *not* disable idle
suspension. Use a positive value.

## What counts as activity

Keyboard input, mouse button presses (including double/triple click),
pointer motion, scrolling, touch events, and focus changes. Background
work (SNMP polling, AI responses, timers) does not reset the clock.

------------------------------------------------------------------------

# Automatic Execution and Policies

Implemented in `autoexec.c`, `policy_dao.c`, and `policy_manager_gui.c`.

## Flow

1.  AI-generated command lines are placed in a FIFO queue.
2.  For each line the binary name is extracted and validated as an
    executable.
3.  The policy for that binary is looked up in `command_policy`; if there
    is none, the wildcard policy `*` is tried; if there is still none, the
    result is **APPROVE**.
4.  Action by policy type:
    -   **ALLOW:** the command runs immediately and an
        `[Auto-Execute ALLOW]` banner is shown.
    -   **BLOCK** (or `DENY`): the command is refused.
    -   **APPROVE:** the queue pauses and a confirmation dialog appears.
5.  The dialog offers **ALLOW (Execute Command)** or **BLOCK (Deny
    Execution)** and a checkbox *Remember policy choice for this binary*,
    which writes the choice as a policy.
6.  The next queued command is dispatched when the previous one finishes
    (queue is advanced on VTE child exit).

Up to 16 confirmation dialogs (`MAX_DLG`) can be open; further commands are
dropped.

## Enable

``` text
/autoexe on
```

or the Toggles menu. Show the pending queue:

``` text
/show queue
```

## Policy Manager and commands

Open `Managers → Policy Manager` or `/open policy manager`.

``` text
policy list
policy find <command>
policy add <command|*> <ALLOW|BLOCK|APPROVE> <LOW|MEDIUM|HIGH|CRITICAL>
policy delete <command>
```

The verb is case-insensitive. Policies live in the `command_policy`
table (columns `command_name`, `policy_type`, `risk_level`).

> Auto Execute lets AI-generated text reach a real shell. Treat it as a
> privileged feature. See [Security Notes](#security-notes).

------------------------------------------------------------------------

# Noise Filtering

Noise filtering keeps repetitive terminal chatter out of the Tee data and
AI context.

-   Patterns are stored in the `noise_filters` table and cached in memory.
-   A line is dropped when it **contains** a stored pattern as a literal
    substring. Patterns are *not* treated as regular expressions, even
    though one menu label says "Regex".
-   Typical targets: repeated prompts, shell redraw noise, status lines,
    known log chatter.

## GUI

`Managers → Noise Filter Manager`, or `/open noise manager`. The manager
lists, adds, and deletes patterns.

## Commands

``` text
/noise filter on|off|status
/noise add <pattern>
/noise list
/noise reload
/noise delete <pattern>     (placeholder - not implemented yet)
```

`/noise delete` currently prints a "to be announced" message. Delete
patterns from the Noise Filter Manager instead.

------------------------------------------------------------------------

# Smart Cache

Implemented in `gemini_cache.c`. It provides cache-aware handling of AI
context for Gemini and can be invalidated or rebuilt on demand.

``` text
/smart cache on|off|status
/invalidate cache
```

The cache is initialized at startup.

------------------------------------------------------------------------

# Rate Limiting

Implemented in `ratelimit.c`. The default is 20 requests per minute.

``` ini
rpm=20
```

``` text
/ratelimit on|off|status
/rpm 10
```

Useful with providers or tiers that enforce request limits.

------------------------------------------------------------------------

# AI Retry

Implemented in `ai_retry.c`.

``` ini
ai_retry_enabled=0
ai_retry_max_retries=3
ai_retry_delay_sec=5
```

``` text
/autoretry on|off|status
/retry times <number>
/retry delay <seconds>
```

The GUI offers `AI Controls → AI Retry Options` with an enable item and
dialogs for max attempts (1-10) and delay (1-60 seconds).

------------------------------------------------------------------------

# SNMP Monitoring

Implemented in `snmp_manager.c` and `snmp_manager_gui.c` using Net-SNMP.

## SNMP Target Manager

`Managers → SNMP Manager`, or `/open snmp manager`. Supports add, edit,
delete, toggle active state, refresh, multi-row selection, column sorting,
and poll-delay adjustment. Each target has an ID, label, IP address,
community, OID string, last value, and active state.

## Poll interval

The GUI slider covers roughly 10 to 3600 seconds.

``` ini
snmp_poll_interval=60
```

## Commands

``` text
/snmp cmd list
/snmp cmd add <label> <ip> <community> <oid>
/snmp cmd edit <id> <label> <ip> <community> <oid>
/snmp cmd delete <id>

/snmp poller start
/snmp poller restart

/snmp enable on|off|status      (send SNMP payload to AI)
/snmp force                     (flush SNMP payload to AI now)
/snmp dump                      (dump raw SNMP data to the AI pane)
/snmp ticker on|off|status
```

`/snmp poller stop` has been removed from the source (the old stop path
was disabled as unreliable). `restart` only starts the poller if it is not
already running.

SNMP polling and sending SNMP to the AI are separate functions; both must
be on (with at least one active target) for SNMP data to reach the AI.

------------------------------------------------------------------------

# Sessions and Persistent Context

Implemented in `session_manager.c` and `session_manager_gui.c`. Each
session has a UUID.

``` text
/session new
/session list
/session show
/session load <UUID>
/session default <UUID>
/session no default
/session description <text>
/session delete <UUID>
/session sync
/session config on|off|status
/session read from global on|off|status
/session write to global on|off|status
```

## Session Manager GUI

`Managers → Session Manager`: create, load, set default, delete,
rename/describe, refresh.

## Session-stored settings

Saved to the `sessions` row: debug, Tee, Auto Reply, Auto Execute, rate
limiting, Smart Cache, noise filtering, XML payload tagging, global
read/write, and session-config loading. `/session sync` writes the current
values; they are also synced on exit.

## Global vs strict session context

When global reading is off, only the current session's context is used.
When global writing is off, the session's context is not written to the
global (all-zero UUID) stream.

------------------------------------------------------------------------

# History Manager

`Managers → History Manager`. Displays stored history, can filter to the
current session, and can delete or clear rows. Table: `aiterm_history`.

You can also print recent history with `/history` or
`AI Controls → System & Help → Database History Logs`.

------------------------------------------------------------------------

# Exporting Sessions

`File → Export Session…` offers a content choice (AI pane, terminal, or
both) and four formats:

``` text
Plain Text (.txt)   HTML (.html)   JSON (.json)   XML (.xml)
```

The export reads the active tab's VTE scrollback and the AI text buffer.

------------------------------------------------------------------------

# Printing

`File → Print…` prepares the current session content for GTK printing
(`print.c`).

------------------------------------------------------------------------

# Multi-Tab Terminal

A `GtkNotebook` holds up to **32** terminal tabs (`MAX_TABS`).

-   New tabs are labelled `pts/1`, `pts/2`, ... in creation order.
-   Each tab has a **×** close button.
-   **Double-clicking** the tab bar opens a new tab.
-   **Right-click** in a terminal shows **New Tab / Copy / Paste**.
-   Ctrl+Tab moves focus: AI input → active terminal → AI view.
-   Fonts and transparency are applied to the newly selected tab.
-   If you close the last tab, a fresh one is created.
-   Each tab runs its own `/bin/bash`, and Tee captures each tab
    separately (see [Tee Capture and Tabs](#tee-capture-and-tabs)).

## Not wired up yet

-   The per-tab flag array (`TabSettings`) exists, but the handler that
    would copy a tab's toggles into the active settings when you switch
    tabs is **disabled** in `gui.c`. Toggles such as Tee, Auto Reply, and
    Auto Execute are effectively global across tabs.
-   *Rename Tab* and *Close Tab* context-menu items are defined, but their
    popup hook is not connected, and tab rename is still a TODO.
-   Per-tab session UUIDs are not assigned.

------------------------------------------------------------------------

# Other GUI Features

-   **Command recall:** Up/Down arrows in the AI input recall earlier
    local commands.
-   **File analysis:** the paperclip button opens a file chooser and sends
    the file's text to the AI as a `FILE ANALYSIS` request.
-   **Copy button:** copies the full AI string contents.
-   **Preferences** (`Tools → Preferences`): terminal and AI pane
    transparency and fonts.

``` ini
term_transparency=0.800000
ai_transparency=0.800000
terminal_font=Monospace 10
ai_font=Monospace 10
```

-   A dark GTK theme is forced for all windows.
-   Token usage/status area and Operational Status (`/status`).

------------------------------------------------------------------------

# XML Payload Tagging

Wraps payload sections in XML-style tags (for example `<tee>`,
`<history>`, SNMP, user text) so the AI can tell sources apart.

``` text
/xml tagging on|off|status
```

``` ini
xml_tagging=1
```

This is independent of XML *export*.

------------------------------------------------------------------------

# Debugging

``` bash
./aiterm --debug
./aiterm --debug 2>aiterm-debug.log
```

or at run time:

``` text
/debug on|off|status
```

Color control:

``` bash
./aiterm --color
./aiterm --bw
```

``` text
/color on|off|status
```

Color is used only when stderr is a TTY. Debug output goes to stderr.

------------------------------------------------------------------------

# Command Reference

Commands are typed in the AI input line. The leading `/` is optional.
Matching is case-insensitive and by prefix against the command registry
(first match wins), so keep the exact command words.

## Core

``` text
/help
/legacy help
/extended help
/features
/version
/status
/hw
/history
/clear
/directives
/command line help
/exit
```

## Toggles

``` text
/tee on|off|status
/autoreply on|off|status
/autoexe on|off|status
/auto all on|off|status
/autoretry on|off|status
/debug on|off|status
/color on|off|status
/smart cache on|off|status
/ratelimit on|off|status
/rpm <number>
/xml tagging on|off|status
```

## Provider and configuration

``` text
/provider
/list models
/load config
/save config
/open provider manager
/close provider manager
/open provider api keys
```

## Cache, state, database

``` text
/invalidate cache
/reset state
/reset db
```

## Sessions

``` text
/session new | list | show | sync
/session load <UUID>
/session description <text>
/session default <UUID>
/session no default
/session delete <UUID>
/session config on|off|status
/session read from global on|off|status
/session write to global on|off|status
```

## Managers

``` text
/open session manager    /close session manager
/open history manager    /close history manager
/open noise manager      /close noise manager
/open policy manager     /close policy manager
/open snmp manager       /close snmp manager
/open provider manager   /close provider manager
```

## Noise

``` text
/noise filter on|off|status
/noise add <pattern>
/noise list
/noise reload
/noise delete <pattern>      (not implemented)
```

## Policy

``` text
policy list
policy find <command>
policy add <command|*> <ALLOW|BLOCK|APPROVE> <LOW|MEDIUM|HIGH|CRITICAL>
policy delete <command>
```

## SNMP

``` text
/snmp cmd list|add|edit|delete ...
/snmp enable on|off|status
/snmp force
/snmp dump
/snmp poller start|restart
/snmp ticker on|off|status
```

## AI retry

``` text
/retry times <number>
/retry delay <seconds>
```

## Execution queue

``` text
/show queue
```

------------------------------------------------------------------------

# Command-Line Options

| Option                | Purpose                                                    |
|-----------------------|------------------------------------------------------------|
| `--help`              | Show command-line help                                     |
| `--version`           | Print version, build ID, build time                        |
| `--debug`             | Verbose logging to stderr                                  |
| `--color`             | Colorize debug output (if stderr is a TTY)                 |
| `--bw`                | Force black-and-white debug output                         |
| `--features`          | Show feature/changelog text                                |
| `--highlights`        | Show application highlights                                |
| `--directives`        | Show the AI directives                                     |
| `--provider`          | Print the active provider, model, protocol, URL, endpoint  |
| `--list-models`       | List available Gemini models (needs master key)            |
| `--master=<key>`      | Supply the config-encryption master key                    |
| `--crypt-pw=<text>`   | Encrypt a plaintext secret and print it                    |
| `--decrypt-pw`        | Print all decrypted provider keys and the DB password      |

Examples:

``` bash
./aiterm --version
./aiterm --provider
./aiterm --master='secret' --debug
./aiterm --master='secret' --crypt-pw='database-password'
```

Options are processed in order and several (`--version`, `--help`,
`--provider`, `--features`, `--crypt-pw`, `--decrypt-pw`) exit
immediately after printing.

------------------------------------------------------------------------

# Environment Variables

| Variable                        | Purpose                                                       |
|---------------------------------|---------------------------------------------------------------|
| `AITERM_MASTER_KEY`             | Master key (fallback when `--master` is not given)            |
| `AITERM_CONFIG`                 | Path of the config file (checked first)                       |
| `AITERM_SQL_DIR`                | Directory of schema `*.sql` scripts                           |
| `AITERM_DEBUG`                  | `1`/`0` - enable debug mode                                   |
| `AITERM_COLOR`                  | `1`/`0` - colorized debug output                              |
| `AITERM_PROVIDER`               | Override the provider name                                    |
| `AITERM_MODEL`                  | Override the model                                            |
| `AITERM_PROVIDER_BASE_URL`      | Provider transport override                                   |
| `AITERM_PROVIDER_ENDPOINT`      | Provider transport override                                   |
| `AITERM_PROVIDER_AUTH_HEADER`   | Provider transport override                                   |
| `AITERM_PROVIDER_AUTH_SCHEME`   | Provider transport override                                   |
| `AITERM_PROVIDER_QUERY_KEY`     | Provider transport override                                   |
| `<NAME>_KEY` / `<NAME>_API_KEY` | API key fallback when the config has no key for that provider (e.g. `GEMINI_KEY` or `GEMINI_API_KEY`) |

Saved Provider Manager values override the built-in defaults.

------------------------------------------------------------------------

# Configuration Reference

Keys written by `save_config()`:

## General

``` ini
color=0
provider=gemini
model=gemini-flash-latest
```

## Provider transport

``` ini
provider_base_url=
provider_endpoint=
provider_auth_header=
provider_auth_scheme=
provider_query_key=
provider_api_key_in_query=0
```

## Encrypted credentials

``` ini
OPENAI_KEY=<AES-256-CBC hex>
GEMINI_KEY=<AES-256-CBC hex>
GROQ_KEY=<AES-256-CBC hex>
OPENROUTER_KEY=<AES-256-CBC hex>
MISTRAL_KEY=<AES-256-CBC hex>
OLLAMA_KEY=<AES-256-CBC hex>
CUSTOM_KEY=<AES-256-CBC hex>
db_pass=<AES-256-CBC hex>
```

A legacy `api_key=` line is read but never written.

## Database

``` ini
db_host=localhost
db_user=root
db_name=aiterm_db
```

## GUI

``` ini
term_transparency=0.800000
ai_transparency=0.800000
terminal_font=Monospace 10
ai_font=Monospace 10
```

## Runtime toggles

``` ini
tee_enabled=0
autoreply_enabled=0
auto_execute_enabled=0
ratelimit_enabled=0
smart_cache_enabled=0
write_to_global=0
read_from_global=1
noise_filter_enabled=0
debug_mode=0
send_snmp_payload=0
snmp_ticker_enabled=1
xml_tagging=0
load_from_session=0
```

## Rate limit, SNMP, retry, idle

``` ini
rpm=20
snmp_poll_interval=60
ai_retry_enabled=0
ai_retry_max_retries=3
ai_retry_delay_sec=5
idle_timeout_minutes=10
```

------------------------------------------------------------------------

# Troubleshooting

## `pkg-config` cannot find GTK / VTE

``` bash
sudo apt install libgtk-3-dev libvte-2.91-dev
pkg-config --modversion gtk+-3.0
pkg-config --modversion vte-2.91
```

## `uuidgen: command not found`

``` bash
sudo apt install uuid-runtime
```

## Net-SNMP headers missing

``` bash
sudo apt install libsnmp-dev
```

## Build error: implicit declaration of a function

Usually a stale or mismatched header. Run `make clean && make`. If a
`make` warning mentions file timestamps in the future, fix the clock or
`touch *.c *.h` first.

## aiterm exits immediately at startup

The startup network check requires a non-loopback IPv4 interface. With
only `lo` configured, aiterm exits. Bring up a network interface (a
virtual one is fine).

## API key errors

``` bash
./aiterm --master='YOUR_MASTER_KEY' --decrypt-pw
./aiterm --master='YOUR_MASTER_KEY' --provider
```

Check that: the master key is correct, the key for the *selected*
provider (e.g. `GEMINI_KEY`) exists, the model name is valid, and the
endpoint is reachable. Use the Provider Manager's test button.

## Database errors

``` bash
./aiterm --debug 2>aiterm-debug.log
```

Look for `[DB]` lines. Confirm the host, user, encrypted password and
database name, that the schema scripts exist in the SQL directory
(`AITERM_SQL_DIR` or `/usr/share/aiterm/db`), and try `/reset db`.

## Tee does not capture a tab

-   Confirm `/tee status` is ON.
-   Output is flushed after about 2 seconds of silence in that tab.
-   Use `Tools → Flush Tee Buffer` to force it.
-   Remember a manual question uses the **focused** tab's data.

## AI describes aiterm's own debug logs

Debug output (stderr) is appearing in a terminal tab that Tee is
capturing. Redirect it (`2>file`) or ask from a different tab.

## SNMP does not produce AI traffic

Check all three layers: poller running, at least one active target, and
*Send SNMP Payload to AI* on.

``` text
/snmp cmd list
/snmp enable status
/snmp ticker status
/snmp poller start
```

## Idle mode too aggressive / does not wake

Raise `idle_timeout_minutes` (a positive number; 0 means the 10-minute
default). With `--debug`, look for `aiterm IDLE` messages.

------------------------------------------------------------------------

# Development Notes

-   Written in C with GTK 3, GLib, POSIX threads, libcurl, and json-c.
-   UI updates are scheduled back onto the GTK main thread; AI and SQL
    work runs on worker threads.
-   One global MariaDB connection, protected by a mutex.
-   Provider selection is centralized so Tee, SNMP, and the AI worker do
    not each need provider-specific dispatch.
-   The idle subsystem changes runtime booleans directly (not through the
    menu-command pipeline) so that suspension is never persisted as a
    user choice.
-   Tee capture state lives on each VTE widget (`g_object_set_data`), and
    each tab's watcher timer holds a `GWeakRef` to its terminal so closing
    a tab ends the timer safely.
-   `mkpkg.sh` creates a source `.tar.gz` named from the parent directory
    layout (`<project>-<version>.tar.gz`).
-   `show-source.sh`, `aiterm-test.py`, and `aiterm-test2.py` are
    development helpers.

------------------------------------------------------------------------

# Project File Map

| File                                         | Purpose                                         |
|----------------------------------------------|-------------------------------------------------|
| `main.c` / `main.h`                          | Startup, shutdown, version string               |
| `gui.c` / `gui.h`                            | Main GTK window, tabs, `AppContext` definitions |
| `terminal.c` / `terminal.h`                  | VTE setup, per-tab Tee capture and timers       |
| `menu.c` / `menu.h`                          | Menu bar, preferences, file dialogs             |
| `toggles.c` / `toggles.h`                    | Toggle routing                                  |
| `commands.c` / `commands.h`                  | Command registry, CLI option parsing            |
| `update.c` / `update.h`                      | AI request path, local commands, policy cmds    |
| `ai_provider.c` / `.h`                       | Provider abstraction                            |
| `provider_manager_gui.c` / `.h`              | Provider Manager window                         |
| `provider_keys.c` / `.h`                     | API keys reference window                       |
| `gemini.c` / `.h`                            | Gemini `generateContent` client                 |
| `openai.c` / `.h`                            | OpenAI-compatible client                        |
| `auto_chunk.c` / `.h`                        | Context budgeting and chunking                  |
| `gemini_cache.c` / `.h`                      | Smart Cache                                     |
| `ai_retry.c` / `.h`                          | AI retry engine                                 |
| `ratelimit.c` / `.h`                         | Request limiter                                 |
| `tee_handler.c` / `.h`                       | Tee accumulator, flush, AI dispatch             |
| `noisefilter.c` / `.h`                       | Noise filtering engine                          |
| `noise_filter_manager_gui.c` / `.h`          | Noise Filter Manager                            |
| `autoexec.c` / `.h`                          | Execution queue and confirmation dialogs        |
| `policy_dao.c` / `.h`                        | Policy database access                          |
| `policy_manager_gui.c` / `.h`                | Policy Manager                                  |
| `snmp_manager.c` / `.h`                      | SNMP engine                                     |
| `snmp_manager_gui.c` / `.h`                  | SNMP Target Manager                             |
| `session_manager.c` / `.h`                   | Session persistence                             |
| `session_manager_gui.c` / `.h`               | Session Manager                                 |
| `history_manager_gui.c` / `.h`               | History Manager                                 |
| `idle.c` / `idle.h`                          | Idle watchdog                                   |
| `export.c` / `.h`                            | Session export                                  |
| `print.c` / `.h`                             | GTK printing                                    |
| `crypto.c` / `.h`                            | AES-256-CBC credential encryption               |
| `config.c` / `.h`                            | Config load/save                                |
| `status.c` / `.h`                            | Runtime status report                           |
| `xml_tagging.c` / `.h`                       | Payload tagging                                 |
| `help.c` / `.h`                              | Help, features, CLI help text                   |
| `utils.c` / `.h`                             | Utilities, DB init, provider config             |
| `resources.xml`, `aiterm-icon.png`           | GResource definition and icon                   |
| `build_id.h`, `resources.c`                  | Generated at build time                         |
| `Makefile`                                   | Build and install                               |
| `aiterm.conf.example`                        | Minimal config template                         |
| `mkpkg.sh`, `show-source.sh`                 | Packaging / source listing helpers              |
| `aiterm-test.py`, `aiterm-test2.py`          | Test/helper scripts                             |

------------------------------------------------------------------------

# Security Notes

## Protect your config file

Encryption is not a substitute for permissions.

``` bash
chmod 600 ~/.config/aiterm.conf     # or /etc/aiterm.conf
```

aiterm does not set the mode when it saves; set it yourself.

## Protect the master key

Avoid putting the key on the command line where shell history records it.
Instead:

``` bash
export AITERM_MASTER_KEY='secret'
./aiterm
unset AITERM_MASTER_KEY
```

Shell history behavior varies, so use what suits your environment.

## Auto Execute

Commands generated by an AI can enter a real shell. Use the Policy Manager
and the confirmation dialog, prefer `BLOCK`/`APPROVE` as the default
posture, and think twice about wildcard `*` ALLOW rules.

## Terminal capture

Tee sends terminal text (including anything you type or display, such as
secrets in command output) to the configured AI provider and stores it in
the database. Disable Tee before handling sensitive data, and use noise
filters where appropriate.

## SNMP community strings

They are credentials, stored in `snmp_targets`. Restrict database access.

## `--decrypt-pw`

Never run it in a shared terminal, recorded session, CI log, or screenshot
workflow.

------------------------------------------------------------------------

# Known Limitations and Source-Tree Caveats

This README documents the source as it exists rather than claiming
functionality that is not present.

## No bundled schema

The C code has no `CREATE TABLE` statements and the reviewed source
archive contains no `db/` directory or `.sql` files. You must provide
schema scripts for `aiterm_history`, `sessions`, `snmp_targets`,
`noise_filters`, `command_policy`, and `relevance_triggers`. Older README
text that said the application creates these tables, or that referred to
`schema.sql`, is obsolete.

## Per-tab settings are not wired

Tee *capture* is per tab, but the toggle flags are global (the
tab-switch handler that would swap them is disabled), per-tab session
UUIDs are not assigned, and background-tab captures are saved under the
session that is current when they flush. Captures from several tabs share
one accumulator, distinguished only by the `[tab pts/N]` stamp.

## Tab rename and tab context-menu items

*Rename Tab* is a TODO and the *Rename/Close Tab* popup items are not
connected. Use the × button to close a tab.

## `/noise delete`

A placeholder. Use the Noise Filter Manager.

## Noise patterns are substrings

The menu text mentions "Regex", but matching is a plain substring test.

## SNMP poller cannot be stopped from a command

`/snmp poller stop` was removed; only `start`/`restart` (which starts the
poller if it is not already running) exist.

## Idle timeout zero

`idle_timeout_minutes=0` is accepted by the loader but replaced with the
10-minute default.

## Auto All

A command (`/auto all`) but not a separate GUI toggle.

## Network requirement at startup

aiterm exits at startup if no non-loopback IPv4 interface is found; there
is no command-line option to override this.

## Distribution-specific Makefiles

Only the single `Makefile` is present. The old `Makefile.debian` and
`Makefile.opensuse` are no longer part of the tree.

## Build verification

Build on the target Linux system; the GTK/VTE/MariaDB/Net-SNMP
development environment is external to the repository.

------------------------------------------------------------------------

# Recommended First-Run Procedure

### 1. Install development dependencies

(See [Build Dependencies](#build-dependencies).)

### 2. Build

``` bash
make clean
make -j"$(nproc)"
```

### 3. Prepare the database

Create the MariaDB database/user and make sure your schema `*.sql` scripts
are in `/usr/share/aiterm/db` (or point `AITERM_SQL_DIR` at them).

### 4. Create the configuration

``` bash
cp aiterm.conf.example ~/.config/aiterm.conf
chmod 600 ~/.config/aiterm.conf
```

### 5. Generate encrypted credentials

``` bash
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='YOUR_API_KEY'
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='YOUR_DB_PASSWORD'
```

### 6. Edit the config

``` ini
provider=gemini
model=gemini-flash-latest
GEMINI_KEY=<encrypted API key>

db_host=127.0.0.1
db_user=aiterm
db_pass=<encrypted DB password>
db_name=aiterm_db
```

### 7. Start aiterm

``` bash
./aiterm --master='YOUR_MASTER_KEY'
# or
export AITERM_MASTER_KEY='YOUR_MASTER_KEY'
./aiterm
```

### 8. Verify the provider

Open `Managers → AI Provider Manager` and use the test operation, or run
`./aiterm --provider`.

### 9. Verify the database

``` text
/session new
/session show
/session list
```

### 10. Verify the toggles

``` text
/tee status
/autoreply status
/autoexe status
/snmp enable status
/snmp ticker status
/autoretry status
/ratelimit status
```

### 11. Test Tee across tabs

1.  `/tee on`
2.  Open a second tab and run a command; wait about 2 seconds or use
    `Tools → Flush Tee Buffer`.
3.  Switch back to the first tab, run another command, and ask the AI
    "what do you see". It should describe the focused tab's output.

------------------------------------------------------------------------

# License

See `LICENSE` for the license applicable to this project.

------------------------------------------------------------------------

## Project Status

This README documents the source tree reviewed for **0.9.12-gamma**. The
source tree is the authoritative reference for behavior when this
document and older historical notes disagree.

Project author: Peter Talbott. Source: <https://github.com/pet350/aiterm>
