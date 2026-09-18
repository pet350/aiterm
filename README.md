# aiterm

## The AI-Augmented Terminal Emulator

**aiterm** is a Linux desktop terminal emulator written in C using GTK 3
and VTE, with a dedicated AI pane alongside the terminal. It combines
normal shell work, persistent MariaDB-backed sessions, AI-assisted
terminal analysis, optional automatic command execution, SNMP telemetry,
provider abstraction, caching, noise filtering, rate limiting,
export/printing, and session-aware configuration.

> **Current source tree:** 0.9.10-beta / September 2026\
> **Primary build system:** GNU Make\
> **Primary configuration file:** `/etc/aiterm.conf`

The project is designed around a simple principle:

> The terminal remains a normal terminal. AI assistance is an additional
> layer that can observe selected terminal activity, analyze it,
> maintain context, and optionally act on explicitly enabled workflows.

------------------------------------------------------------------------

## Table of Contents

1.  [Architecture at a Glance](#architecture-at-a-glance)
2.  [Major Features](#major-features)
3.  [System Requirements](#system-requirements)
4.  [Build Dependencies](#build-dependencies)
5.  [Building from Source](#building-from-source)
6.  [Installing and Running](#installing-and-running)
7.  [Configuration](#configuration)
8.  [Encrypted Credentials](#encrypted-credentials)
9.  [AI Providers](#ai-providers)
10. [Provider Manager](#provider-manager)
11. [MariaDB Database](#mariadb-database)
12. [Terminal and AI Workflow](#terminal-and-ai-workflow)
13. [System Toggles](#system-toggles)
14. [Idle Suspension](#idle-suspension)
15. [Automatic Execution and
    Policies](#automatic-execution-and-policies)
16. [Noise Filtering](#noise-filtering)
17. [Smart Cache](#smart-cache)
18. [Rate Limiting](#rate-limiting)
19. [AI Retry](#ai-retry)
20. [SNMP Monitoring](#snmp-monitoring)
21. [Sessions and Persistent Context](#sessions-and-persistent-context)
22. [History Manager](#history-manager)
23. [Exporting Sessions](#exporting-sessions)
24. [Printing](#printing)
25. [Multi-Tab Terminal](#multi-tab-terminal)
26. [Command Reference](#command-reference)
27. [Command-Line Options](#command-line-options)
28. [Environment Variables](#environment-variables)
29. [Configuration Reference](#configuration-reference)
30. [Troubleshooting](#troubleshooting)
31. [Development Notes](#development-notes)
32. [Project File Map](#project-file-map)
33. [Security Notes](#security-notes)
34. [Known Limitations and Source-Tree
    Caveats](#known-limitations-and-source-tree-caveats)
35. [License](#license)

------------------------------------------------------------------------

# Architecture at a Glance

aiterm is composed of several cooperating subsystems:

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
                 |                           |
        +--------v---------------------------v--------+
        |            Application Context              |
        |                 AppContext                  |
        +----+-----------+-----------+-----------+----+
             |           |           |           |
        +----v---+   +---v----+  +---v----+  +---v----+
        |  Tee   |   | Session|  |  SNMP  |  | Auto  |
        | Engine |   | / DB   |  | Poller |  | Exec   |
        +----+---+   +---+----+  +---+----+  +---+----+
             |           |           |           |
             +-----------+-----------+-----------+
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

The provider abstraction currently distinguishes Gemini's
`generateContent` protocol from OpenAI-compatible Chat Completions
providers.

GTK is event-driven, and aiterm uses GTK/GLib timers, signals, and event
handlers for the GUI, ticker, idle watchdog, and other asynchronous
operations. GTK's official documentation describes this event-driven
model and the `pkg-config` build mechanism used by the project.\
See the [GTK 3 API documentation](https://docs.gtk.org/gtk3/) and [GTK
compilation documentation](https://docs.gtk.org/gtk3/compiling.html).

------------------------------------------------------------------------

# Major Features

## Dual-pane interface

The main window contains:

-   A VTE-backed terminal pane.
-   An AI conversation/output pane.
-   A token/status area.
-   An optional SNMP ticker.
-   Multi-tab terminal support.

The terminal remains usable as a normal shell while AI features are
selectively enabled.

## AI provider abstraction

The AI request layer is centralized in `ai_provider.c`.

Supported provider names in the current source:

``` text
openai
gemini
groq
openrouter
mistral
ollama
custom
```

Gemini uses its own `generateContent` protocol. The other built-in
providers use the OpenAI-compatible request path.

## Persistent sessions

Sessions use UUIDs and are stored in MariaDB. Sessions can be:

-   Created.
-   Listed.
-   Loaded.
-   Renamed/described.
-   Deleted.
-   Marked as the default session.
-   Used to restore selected configuration values.

## Terminal capture / Tee

Tee mode captures terminal activity for AI analysis. It can operate
together with Auto Reply so terminal output can be analyzed
automatically.

## Automatic execution

AI-generated command payloads can be placed into an execution queue. The
application includes a policy system and confirmation dialogs around
command execution.

## SNMP telemetry

The SNMP subsystem can:

-   Poll configured targets.
-   Track configured OIDs.
-   Display the most recent values.
-   Send accumulated SNMP data to the AI.
-   Display a scrolling SNMP ticker.
-   Force an immediate AI payload flush.
-   Manage targets through a GUI or commands.

## Smart Cache

The Gemini cache subsystem can maintain reusable context/cache
information and invalidate or rebuild it when required.

## Noise filtering

A database-backed noise filter can suppress repetitive terminal output
before it reaches downstream AI/telemetry processing.

## Rate limiting

A request-per-minute limiter is available to prevent excessive AI
request frequency.

## AI Retry

Failed AI operations can optionally be retried with configurable:

-   Enable/disable state.
-   Maximum retry count.
-   Delay between attempts.

## Export and printing

The active session can be exported as:

-   Plain text.
-   HTML.
-   JSON.
-   XML.

The application also contains a print workflow.

## Encrypted credentials

The configuration writer encrypts the AI API key and MariaDB password
using the project's AES-256-CBC crypto functions.

------------------------------------------------------------------------

# System Requirements

The current source tree is intended for Linux.

Recommended baseline:

-   64-bit Linux.
-   GCC or another C compiler compatible with the source.
-   GNU Make.
-   GTK 3.
-   VTE 2.91.
-   GLib 2.x.
-   libcurl.
-   json-c.
-   MariaDB/MySQL client development libraries.
-   OpenSSL/libcrypto.
-   UUID development library.
-   Net-SNMP development libraries.
-   `pkg-config`.
-   `glib-compile-resources`.

A graphical Linux session is required because aiterm creates a GTK
desktop application.

------------------------------------------------------------------------

# Build Dependencies

## Debian / Ubuntu

The primary Makefile automatically detects either `libmariadb` or
`mariadb` through `pkg-config`.

A practical development installation is:

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

If your distribution packages MariaDB development files differently,
verify the available pkg-config module:

``` bash
pkg-config --list-all | grep -Ei 'mariadb|mysql'
```

Then verify the major dependencies:

``` bash
pkg-config --modversion gtk+-3.0
pkg-config --modversion glib-2.0
pkg-config --modversion vte-2.91
pkg-config --modversion libcurl
pkg-config --modversion json-c
```

The primary Makefile also links against:

``` text
pthread
curl
crypto
uuid
netsnmp
```

## openSUSE

Install the corresponding development packages for:

``` text
gtk3
vte
glib
libcurl
json-c
mariadb-connector-c
openssl
libuuid
net-snmp
pkg-config
make
gcc
```

The repository includes `Makefile.opensuse`, but the primary `Makefile`
is the most complete build definition in this source tree.

------------------------------------------------------------------------

# Building from Source

Change into the source directory:

``` bash
cd /path/to/aiterm
```

Generate and build:

``` bash
make
```

For a clean rebuild:

``` bash
make clean
make -j"$(nproc)"
```

The default `Makefile` builds the full object list, including the
current idle subsystem:

``` text
idle.o
```

The build also generates `resources.c` from:

``` text
resources.xml
aiterm-icon.png
```

using `glib-compile-resources`.

## Build ID generation

The Makefile generates `build_id.h` with:

-   A UUID build ID.
-   A build timestamp.

The Makefile invokes:

``` bash
uuidgen
```

If `uuidgen` is missing on Debian/Ubuntu:

``` bash
sudo apt install uuid-runtime
```

If you already have a generated `build_id.h`, `make clean` removes it
and the next build regenerates it.

## Verify the build

After a successful build:

``` bash
./aiterm --version
```

You can also inspect:

``` bash
./aiterm --features
./aiterm --help
./aiterm --provider
```

------------------------------------------------------------------------

# Installing and Running

The source tree does not currently provide a complete `make install`
target.

For development/testing, run directly:

``` bash
./aiterm
```

Because the application currently uses `/etc/aiterm.conf` as its
configuration location, an installation intended for normal use should
place the executable somewhere on the user's `PATH` and create the
configuration file with appropriate permissions.

For example:

``` bash
sudo install -m 0755 aiterm /usr/local/bin/aiterm
```

Then create:

``` text
/etc/aiterm.conf
```

A system-wide configuration containing encrypted credentials should be
protected appropriately.

------------------------------------------------------------------------

# Configuration

The application configuration file is:

``` text
/etc/aiterm.conf
```

The repository contains:

``` text
aiterm.conf.example
```

The example file is intentionally minimal. The running application's
`save_config()` function writes the complete configuration format.

## Important configuration behavior

When the application saves configuration, it writes:

``` text
# WARNING: Any changes made to this file will be overwritten
```

Therefore:

-   Manual changes can be overwritten by `save_config()`.
-   Use the application's configuration/provider controls where
    practical.
-   Back up the configuration before major changes.

## Creating the initial configuration

Start with the supplied example:

``` bash
sudo cp aiterm.conf.example /etc/aiterm.conf
sudo chmod 600 /etc/aiterm.conf
```

Then edit the non-secret values:

``` ini
provider=gemini
model=gemini-flash-latest

db_host=127.0.0.1
db_user=aiterm
db_name=aiterm_db
```

Do **not** put plaintext API keys or database passwords into the file.
The current configuration loader expects encrypted values.

------------------------------------------------------------------------

# Encrypted Credentials

aiterm uses the project's crypto functions to encrypt:

-   AI API key.
-   MariaDB database password.

The encryption workflow uses a master key.

## Supply the master key on the command line

``` bash
./aiterm --master='YOUR_MASTER_KEY'
```

## Or use an environment variable

``` bash
export AITERM_MASTER_KEY='YOUR_MASTER_KEY'
./aiterm
```

If neither is supplied, the application prompts for the master key.

## Encrypt a password or API key

``` bash
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='PLAINTEXT_SECRET'
```

The program prints an encrypted string.

Place that encrypted value into the appropriate configuration field:

``` ini
api_key=ENCRYPTED_VALUE
```

or:

``` ini
db_pass=ENCRYPTED_VALUE
```

## Inspect decrypted credentials

The source provides:

``` bash
./aiterm --master='YOUR_MASTER_KEY' --decrypt-pw
```

This prints the decrypted AI key and database password.

**Use this command carefully.** The output is plaintext and should not
be redirected into logs, shell history, screenshots, or shared terminal
recordings.

------------------------------------------------------------------------

# AI Providers

The provider abstraction is implemented by:

``` text
ai_provider.c
ai_provider.h
provider_manager_gui.c
provider_manager_gui.h
```

## Built-in providers

### Gemini

Default Gemini configuration:

``` ini
provider=gemini
model=gemini-flash-latest
```

Gemini uses:

``` text
generateContent
```

and normally supplies the API key as a query parameter.

### OpenAI

OpenAI uses the OpenAI-compatible Chat Completions path.

### Groq

The provider manager supplies a default OpenAI-compatible endpoint and
model.

### OpenRouter

The provider manager includes a default OpenRouter-compatible endpoint.

### Mistral

The provider manager includes a default Mistral endpoint.

### Ollama

The provider manager includes a default local endpoint:

``` text
http://127.0.0.1:11434/v1
```

### Custom

The provider manager also permits a custom provider configuration.

------------------------------------------------------------------------

# Provider Manager

Open:

``` text
Managers → AI Provider Manager
```

or use:

``` text
/open provider manager
```

The manager allows configuration of:

-   Provider name.
-   Model.
-   Base URL.
-   Endpoint.
-   Authentication header.
-   Authentication scheme.
-   API key.
-   Query-string key name.
-   Whether the API key is supplied in the query.
-   Protocol selection.

The built-in provider templates populate sensible defaults.

## Test the provider

The Provider Manager includes a test operation that sends a small test
request and reports whether the provider returned assistant text.

This is useful for validating:

-   Network connectivity.
-   Endpoint.
-   Model name.
-   API key.
-   Protocol selection.

------------------------------------------------------------------------

# Terminal and AI Workflow

The central workflow is:

``` text
Shell command
     |
     v
VTE terminal
     |
     +---- normal terminal use
     |
     +---- Tee capture, if enabled
                |
                v
        Noise filtering
                |
                v
        Context/session logic
                |
                v
           AI provider
                |
                v
             AI pane
```

The user can interact directly with the AI pane input while
independently using the terminal.

## Tee mode

Tee mode captures terminal activity for AI processing.

Toggle:

``` text
/tee on
/tee off
/tee status
```

or use the Toggles menu.

## Auto Reply

Auto Reply enables real-time analysis of captured terminal output.

Toggle:

``` text
/autoreply on
/autoreply off
/autoreply status
```

## Tee and Auto Reply relationship

The GUI logic intentionally couples these features:

-   Turning Tee off also turns Auto Reply off.
-   Turning Auto Reply on also turns Tee on.

This prevents Auto Reply from being enabled without the terminal capture
mechanism it depends on.

------------------------------------------------------------------------

# System Toggles

The **AI → Toggles** menu contains the current runtime switches.

The current source includes:

  -----------------------------------------------------------------------
  Toggle                              Purpose
  ----------------------------------- -----------------------------------
  Real-Time Prompt Analysis           Enables automatic analysis of
                                      relevant terminal/prompt activity

  AI Payload Auto-Execution           Allows AI command payloads to enter
                                      the execution workflow

  Immediate Terminal Capturing / Tee  Captures terminal output for AI
                                      processing

  Debug                               Enables verbose diagnostic output

  XML Payload Tagging                 Adds XML-style boundaries to AI
                                      payload processing

  Noise Filters                       Enables configured terminal-output
                                      suppression rules

  Smart Cache                         Enables the cache subsystem

  Rate Limiting                       Enables request-per-minute
                                      protection

  Read from Global Database           Allows global history/context reads

  Write to Global Database            Allows global session/context
                                      writes

  Send SNMP Payload to AI             Allows SNMP telemetry to be sent to
                                      the AI

  SNMP Ticker                         Enables the scrolling SNMP display

  AI Retry                            Enables automatic retry handling

  Session Config                      Enables session-based configuration
                                      loading
  -----------------------------------------------------------------------

### Important

`Auto All` exists as a command-level feature:

``` text
/auto all on
/auto all off
/auto all status
```

The current GUI Toggles menu does not expose `Auto All` as a separate
check item.

------------------------------------------------------------------------

# Idle Suspension

The current source tree includes a dedicated:

``` text
idle.c
idle.h
```

subsystem.

Its purpose is to stop unattended runtime features from continuing
indefinitely.

## How it works

After the configured period without user activity, aiterm:

1.  Saves the current state of the managed toggles.
2.  Turns those managed runtime booleans off.
3.  Updates the GUI toggle states.
4.  Stops the SNMP ticker timer if it was active.
5.  Leaves the application itself running.
6.  Does not intentionally persist the temporary idle-off state as the
    user's normal configuration.

When user activity resumes:

1.  The previous toggle states are restored.
2.  The SNMP ticker timer is recreated if it had been enabled.
3.  The GUI is synchronized back to the previous state.

This is especially useful for unattended SNMP-to-AI operation because
the SNMP poller can remain alive while the AI-feed toggle is suspended.

## Configure the timeout

The configuration key is:

``` ini
idle_timeout_minutes=5
```

The source accepts values from:

``` text
0 through 1440 minutes
```

For the current testing build, **5 minutes** is the recommended test
value.

## Important source behavior

The current `idle_init()` implementation treats a zero timeout as the
default timeout rather than as a permanent disable. Therefore, until
that behavior is changed in the source, use a positive value when
configuring idle behavior.

## What counts as activity?

The idle event handler watches for user-facing GTK events including:

-   Keyboard input.
-   Mouse button presses.
-   Double/triple clicks.
-   Pointer motion.
-   Scroll events.
-   Touch events.
-   Focus changes.

Background events such as SNMP polling, AI responses, and normal timers
are not intended to reset the user-activity clock.

------------------------------------------------------------------------

# Automatic Execution and Policies

The Auto Execute subsystem is implemented in:

``` text
autoexec.c
autoexec.h
policy_dao.c
policy_dao.h
policy_manager_gui.c
policy_manager_gui.h
```

AI-generated command payloads can be placed into an execution queue.

The execution workflow includes:

-   Command validation.
-   Queueing.
-   Confirmation dialogs.
-   Policy lookup.
-   Risk/action handling.
-   Shell execution.
-   Queue inspection.

## Enable Auto Execute

Use:

``` text
/autoexe on
```

or the Toggles menu.

## Show the execution queue

``` text
/show queue
```

## Policy Manager

Open:

``` text
Managers → Policy Manager
```

or:

``` text
/open policy manager
```

Policy commands include:

``` text
/policy list
/policy find <command>
/policy add <command> <type> <risk>
/policy delete <command>
```

The policy database table is:

``` text
command_policies
```

------------------------------------------------------------------------

# Noise Filtering

Noise filtering is intended to keep repetitive terminal chatter from
becoming AI context.

Examples of useful filter targets include:

-   Repeated prompts.
-   Shell redraw noise.
-   High-frequency status lines.
-   Known repetitive log messages.

## GUI

Open:

``` text
Managers → Noise Filter Manager
```

or:

``` text
/open noise manager
```

The manager supports adding and deleting patterns.

## Commands

``` text
/noise filter on
/noise filter off
/noise filter status

/noise add <pattern>
/noise delete <pattern>
/noise list
/noise reload
```

Patterns are stored through the database-backed noise filter subsystem.

------------------------------------------------------------------------

# Smart Cache

Smart Cache is implemented in:

``` text
gemini_cache.c
gemini_cache.h
```

It provides cache-aware handling for AI context.

Commands:

``` text
/smart cache on
/smart cache off
/smart cache status
/invalidate cache
```

The cache subsystem can be useful when repeated context would otherwise
produce unnecessary repeated provider work.

The cache is initialized during application startup and can be
invalidated manually.

------------------------------------------------------------------------

# Rate Limiting

The rate limiter is implemented in:

``` text
ratelimit.c
ratelimit.h
```

The default startup RPM value in `initialize_booleans()` is:

``` text
20 requests/minute
```

The configuration file can override this with:

``` ini
rpm=20
```

Toggle protection:

``` text
/ratelimit on
/ratelimit off
/ratelimit status
```

Change the RPM ceiling:

``` text
/rpm 10
```

The rate limiter is particularly useful with provider/developer tiers
that enforce request limits.

------------------------------------------------------------------------

# AI Retry

AI Retry is configured through:

``` text
ai_retry.c
ai_retry.h
```

Configuration:

``` ini
ai_retry_enabled=0
ai_retry_max_retries=3
ai_retry_delay_sec=5
```

The GUI provides:

``` text
Toggle AI retry
```

and controls for:

-   Maximum retries.
-   Retry delay.

Commands:

``` text
/autoretry on
/autoretry off
/autoretry status

/retry times <number>
/retry delay <seconds>
```

The current source limits the menu-entered retry count to 1--10 and
delay to 1--60 seconds.

------------------------------------------------------------------------

# SNMP Monitoring

The SNMP subsystem is one of the larger subsystems in aiterm.

Relevant files:

``` text
snmp_manager.c
snmp_manager.h
snmp_manager_gui.c
snmp_manager_gui.h
```

It uses Net-SNMP.

## SNMP Target Manager

Open:

``` text
Managers → SNMP Target Manager
```

or:

``` text
/open snmp manager
```

The GUI supports:

-   Add target.
-   Edit target.
-   Delete target.
-   Toggle target active state.
-   Refresh target list.
-   Multiple row selection.
-   Column sorting.
-   Poller delay adjustment.

Target information includes:

-   ID.
-   Label.
-   IP address.
-   Community.
-   OID string.
-   Last value.
-   Active state.

## Poll interval

The GUI slider supports approximately:

``` text
10 seconds through 3600 seconds
```

The configuration key is:

``` ini
snmp_poll_interval=60
```

The actual value is stored in seconds.

## Start/restart the poller

``` text
/snmp poller start
/snmp poller restart
```

The source currently does not expose the old stop path because that code
was intentionally disabled after being described in the source as
glitchy.

## List SNMP targets

``` text
/snmp cmd list
```

## Add an SNMP target

``` text
/snmp cmd add <label> <ip> <community> <oid>
```

## Edit

``` text
/snmp cmd edit <id> <label> <ip> <community> <oid>
```

## Delete

``` text
/snmp cmd delete <id>
```

## Send SNMP to AI

``` text
/snmp enable on
/snmp enable off
/snmp enable status
```

## Force an SNMP flush

``` text
/snmp force
```

## Dump raw SNMP data

``` text
/snmp dump
```

## SNMP ticker

``` text
/snmp ticker on
/snmp ticker off
/snmp ticker status
```

The ticker scrolls SNMP values through the AI-side display.

------------------------------------------------------------------------

# Sessions and Persistent Context

Session handling is implemented through:

``` text
session_manager.c
session_manager.h
session_manager_gui.c
session_manager_gui.h
```

Each session has a UUID.

## Session commands

Create:

``` text
/session new
```

List:

``` text
/session list
```

Show the current session:

``` text
/session show
```

Load:

``` text
/session load <UUID>
```

Set the default:

``` text
/session default <UUID>
```

Clear the default:

``` text
/session no default
```

Describe/rename:

``` text
/session description <text>
```

Delete:

``` text
/session delete <UUID>
```

Synchronize:

``` text
/session sync
```

## Session Manager GUI

Open:

``` text
Managers → Session Manager
```

The GUI supports:

-   Create.
-   Load.
-   Set default.
-   Delete.
-   Rename/describe.
-   Refresh.

## Session configuration

The following settings can be associated with sessions:

-   Debug mode.
-   Tee.
-   Auto Reply.
-   Auto Execute.
-   Rate limiting.
-   Smart Cache.
-   Noise filtering.
-   XML payload tagging.
-   Global read/write behavior.
-   Session-config loading.

Toggle:

``` text
/session config on
/session config off
/session config status
```

------------------------------------------------------------------------

# Global vs Strict Session Context

Two controls determine how history is shared:

``` text
/session read from global on
/session read from global off

/session write to global on
/session write to global off
```

When global reading is disabled, the application uses stricter
session-specific context.

When global writing is disabled, the active session does not write its
context into the global broadcast/history path.

These settings are also available in the Toggles menu.

------------------------------------------------------------------------

# History Manager

Open:

``` text
Managers → History Manager
```

The History Manager can:

-   Display stored history.
-   Filter to the current session.
-   Delete selected history.
-   Clear history.

The underlying table is:

``` text
aiterm_history
```

History records contain fields including:

-   ID.
-   Role.
-   Content.
-   Tee flag.
-   Session UUID.
-   Sequence ID.
-   Creation timestamp.

------------------------------------------------------------------------

# Exporting Sessions

Use:

``` text
File → Export Session…
```

The export dialog supports selecting:

-   AI pane only.
-   Terminal only.
-   Both.

Supported output formats:

``` text
Plain text
HTML
JSON
XML
```

The export code obtains the active VTE scrollback and the AI text
buffer, then serializes the selected content.

------------------------------------------------------------------------

# Printing

Use:

``` text
File → Print
```

The print subsystem is implemented in:

``` text
print.c
print.h
```

The application prepares the current session content for GTK printing.

------------------------------------------------------------------------

# Multi-Tab Terminal

aiterm uses a GTK notebook for multiple terminal tabs.

Features include:

-   Multiple terminal sessions.
-   Per-tab session UUID.
-   Per-tab system settings.
-   Tab switching.
-   Tab rename menu entry.
-   Tab close menu entry.
-   Double-click tab-bar behavior for creating a new tab.
-   Right-click terminal context menu.

## Right-click terminal menu

The terminal context menu provides:

-   New Tab.
-   Copy.
-   Tab management actions.

## Keyboard shortcut

The source handles:

``` text
Ctrl + Tab
```

for cycling focus between:

``` text
AI input → active terminal → AI view
```

The terminal also supports normal VTE copy/paste behavior.

------------------------------------------------------------------------

# GUI Preferences

The Tools/Preferences workflow provides controls for:

-   Terminal transparency.
-   AI pane transparency.
-   Terminal font.
-   AI font.

Configuration fields:

``` ini
term_transparency=0.8
ai_transparency=0.8
terminal_font=Monospace 10
ai_font=Monospace 10
```

The actual transparency values are floating-point values.

The application also enables GTK's dark-theme preference during startup.

------------------------------------------------------------------------

# XML Payload Tagging

XML tagging can wrap AI payload boundaries in a more explicit structure.

Toggle:

``` text
/xml tagging on
/xml tagging off
/xml tagging status
```

Configuration:

``` ini
xml_tagging=1
```

This is independent of the XML **export** format. Exporting a session as
XML does not require the runtime payload-tagging feature to be enabled.

------------------------------------------------------------------------

# Debugging

Enable debug mode:

``` bash
./aiterm --debug
```

or:

``` text
/debug on
/debug off
/debug status
```

Colorized debug output can be controlled with:

``` bash
./aiterm --color
./aiterm --bw
```

or:

``` text
/color on
/color off
/color status
```

Debug output is primarily written to stderr.

For a terminal session:

``` bash
./aiterm --debug 2>aiterm-debug.log
```

------------------------------------------------------------------------

# Command Reference

aiterm accepts local commands in the AI input path. A command may be
entered with or without the leading `/`.

## Core

``` text
/help
/extended help
/features
/version
/status
/hw
/clear
/exit
```

## AI control

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

## Provider

``` text
/provider
/list models
/open provider manager
/close provider manager
```

## Cache

``` text
/invalidate cache
```

## Sessions

``` text
/session new
/session list
/session show
/session load <UUID>
/session description <text>
/session default <UUID>
/session no default
/session delete <UUID>
/session sync
/session config on|off|status
/session read from global on|off|status
/session write to global on|off|status
```

## Managers

``` text
/open session manager
/close session manager

/open history manager
/close history manager

/open noise manager
/close noise manager

/open policy manager
/close policy manager

/open snmp manager
/close snmp manager

/open provider manager
/close provider manager
```

## Noise

``` text
/noise filter on|off|status
/noise add <pattern>
/noise delete <pattern>
/noise list
/noise reload
```

## Policy

``` text
/policy list
/policy find <command>
/policy add <command> <type> <risk>
/policy delete <command>
```

## SNMP

``` text
/snmp cmd list
/snmp cmd add <label> <ip> <community> <oid>
/snmp cmd edit <id> <label> <ip> <community> <oid>
/snmp cmd delete <id>

/snmp enable on|off|status
/snmp force
/snmp dump
/snmp poller start
/snmp poller restart
/snmp ticker on|off|status
```

## AI retry

``` text
/retry times <number>
/retry delay <seconds>
```

## Database

``` text
/reset db
```

## Execution queue

``` text
/show queue
```

------------------------------------------------------------------------

# Command-Line Options

Run:

``` bash
./aiterm --help
```

Current command-line options include:

  Option                Purpose
  --------------------- ------------------------------------------------
  `--help`              Display command-line help
  `--version`           Display version/build information
  `--debug`             Enable debug logging
  `--color`             Enable colorized debug output
  `--bw`                Disable colorized debug output
  `--features`          Display feature information
  `--highlights`        Display application highlights
  `--master=<key>`      Supply the configuration encryption master key
  `--directives`        Display AI directives
  `--list-models`       Query the Gemini model list
  `--decrypt-pw`        Display decrypted credentials
  `--crypt-pw=<text>`   Encrypt a plaintext secret

Examples:

``` bash
./aiterm --version
```

``` bash
./aiterm --features
```

``` bash
./aiterm --master='secret' --debug
```

``` bash
./aiterm --master='secret' --crypt-pw='database-password'
```

------------------------------------------------------------------------

# Environment Variables

## Master encryption key

``` bash
AITERM_MASTER_KEY
```

Example:

``` bash
export AITERM_MASTER_KEY='your-master-key'
```

## Debug mode

The startup code also reads:

``` bash
AITERM_DEBUG
```

Example:

``` bash
AITERM_DEBUG=1 ./aiterm
```

## Debug color

The startup code reads:

``` bash
AITERM_COLOR
```

Example:

``` bash
AITERM_COLOR=1 ./aiterm
```

## API key fallback

If the configured encrypted API key is unavailable, startup checks:

``` text
GEMINI_API_KEY
OPENAI_API_KEY
```

The provider-specific configuration remains the preferred mechanism.

------------------------------------------------------------------------

# Configuration Reference

The following keys are written by the current `save_config()`
implementation.

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
api_key=<AES-256-CBC encrypted value>
db_pass=<AES-256-CBC encrypted value>
```

## Database

``` ini
db_host=localhost
db_user=root
db_name=aiterm_db
```

## GUI

``` ini
term_transparency=0.8
ai_transparency=0.8
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

## Rate limit

``` ini
rpm=20
```

## SNMP

``` ini
snmp_poll_interval=60
```

## AI retry

``` ini
ai_retry_enabled=0
ai_retry_max_retries=3
ai_retry_delay_sec=5
```

## Idle

``` ini
idle_timeout_minutes=5
```

The current source accepts an idle timeout between 0 and 1440 minutes,
although the current initialization code replaces zero with the default
timeout.

------------------------------------------------------------------------

# MariaDB Database

The application uses MariaDB/MySQL client APIs and connects to the
configured database host during startup.

The database initialization code automatically attempts to create the
configured database:

``` sql
CREATE DATABASE IF NOT EXISTS <db_name>
```

It also automatically creates/migrates several structures.

## Tables created or migrated by the current source

The source explicitly creates:

``` text
aiterm_history
relevance_triggers
command_policies
```

and migrates:

``` text
sessions
```

The source also uses:

``` text
snmp_targets
```

for SNMP configuration.

### Important

The current source tree does **not** contain a `schema.sql` file.

It also does not explicitly create the `sessions` or `snmp_targets`
tables in the database initialization function. Therefore a fresh
deployment must ensure those tables exist with a schema compatible with
the source before using session/SNMP features.

This is an important deployment requirement and should not be hidden
behind a misleading claim that the application is completely
self-initializing.

## Database permissions

The configured database account must have sufficient permissions for the
operations aiterm performs.

At minimum, normal operation requires the ability to:

-   Connect to the configured MariaDB server.
-   Select the application database.
-   Create/migrate the application's tables where applicable.
-   Insert/update/delete application records.
-   Read session/history/SNMP records.

If the account cannot create databases, create `aiterm_db` manually and
grant the application account access.

------------------------------------------------------------------------

# Database Connection Behavior

The current source configures a short connection timeout and uses the
global MariaDB connection shared by application subsystems.

Database operations are protected by application mutexes in the areas
that share the connection.

If the database connection fails, run:

``` text
/reset db
```

and inspect debug output.

------------------------------------------------------------------------

# Troubleshooting

## `pkg-config` cannot find GTK

Example:

``` text
Package gtk+-3.0 was not found
```

Install the GTK development package:

``` bash
sudo apt install libgtk-3-dev
```

Then verify:

``` bash
pkg-config --modversion gtk+-3.0
```

GTK's documentation confirms that GTK applications use `pkg-config` to
obtain compiler and linker flags.\
See https://docs.gtk.org/gtk3/compiling.html

## VTE is missing

Verify:

``` bash
pkg-config --modversion vte-2.91
```

On Debian/Ubuntu:

``` bash
sudo apt install libvte-2.91-dev
```

## `uuidgen: command not found`

Install:

``` bash
sudo apt install uuid-runtime
```

## Net-SNMP headers are missing

Install the Net-SNMP development package, typically:

``` bash
sudo apt install libsnmp-dev
```

## MariaDB pkg-config module missing

Check:

``` bash
pkg-config --list-all | grep -Ei 'mariadb|mysql'
```

The primary Makefile checks for:

``` text
libmariadb
mariadb
mysqlclient
```

## API key errors

Check:

``` bash
./aiterm --master='YOUR_MASTER_KEY' --decrypt-pw
```

Verify that:

-   The master key is correct.
-   `api_key=` contains a valid encrypted value.
-   The selected provider is correct.
-   The model name is valid.
-   The endpoint is reachable.

## Database errors

Run with debug output:

``` bash
./aiterm --debug 2>aiterm-debug.log
```

Look for:

``` text
[DB]
```

messages.

Verify the configuration:

``` ini
db_host=...
db_user=...
db_pass=<encrypted>
db_name=...
```

## SNMP does not produce AI traffic

Check all three layers:

``` text
SNMP Poller
SNMP Target
Send SNMP Payload to AI
```

Useful commands:

``` text
/snmp cmd list
/snmp ticker status
/snmp enable status
/snmp poller start
```

Remember that SNMP polling and sending SNMP data to the AI are separate
functions.

## Idle mode appears not to wake

Activity is detected from GTK user events. Test with:

-   Keyboard input.
-   Mouse click.
-   Pointer movement.
-   Scroll.
-   Focus changes.

If the application remains suspended, enable debug output and look for:

``` text
aiterm IDLE
```

messages.

## Idle mode is too aggressive

Increase:

``` ini
idle_timeout_minutes=15
```

or another positive value.

Because the current implementation treats zero as the default timeout
during initialization, do not use `0` expecting it to disable idle
behavior.

------------------------------------------------------------------------

# Development Notes

## Source language

The project is written primarily in:

``` text
C
```

using GTK 3 and GLib.

## Threading

The application uses:

-   POSIX threads.
-   GTK/GLib main-loop callbacks.
-   Mutexes and condition variables.
-   Background database initialization.
-   Background AI/telemetry work.

UI updates are generally scheduled back onto the GTK main thread.

## Provider abstraction

AI provider selection is intentionally centralized so that:

``` text
Tee
SNMP
AI worker
```

do not each need provider-specific dispatch logic.

## Idle subsystem

The idle subsystem intentionally changes runtime booleans directly
rather than routing every temporary state change through the normal
menu-command pipeline.

This is important because normal toggle operations may synchronize state
to the database. Idle suspension is intended to be temporary.

The state flow is:

``` text
NORMAL
  |
  | no user activity for configured timeout
  v
SUSPENDED
  |
  | user activity
  v
NORMAL
```

The pre-idle states are stored individually so a mixed state such as:

``` text
Tee       ON
AutoExec  OFF
SNMP AI   ON
Ticker    ON
Debug     OFF
Retry     ON
```

can be restored exactly.

------------------------------------------------------------------------

# Project File Map

  File                           Purpose
  ------------------------------ -----------------------------------------------
  `main.c`                       Application startup/shutdown
  `main.h`                       Main application declarations
  `gui.c`                        Main GTK interface
  `gui.h`                        GUI/AppContext definitions
  `terminal.c`                   VTE terminal handling
  `terminal.h`                   Terminal declarations
  `menu.c`                       GTK menus and menu actions
  `menu.h`                       Menu declarations
  `toggles.c`                    Toggle state routing
  `toggles.h`                    Toggle definitions
  `commands.c`                   Local command registry and handlers
  `commands.h`                   Command declarations
  `ai_provider.c`                Provider abstraction
  `ai_provider.h`                Provider abstraction interface
  `provider_manager_gui.c`       Provider configuration GUI
  `provider_manager_gui.h`       Provider manager interface
  `gemini.c`                     Gemini API implementation
  `gemini.h`                     Gemini declarations
  `openai.c`                     OpenAI-compatible API implementation
  `openai.h`                     OpenAI declarations
  `gemini_cache.c`               Smart cache
  `gemini_cache.h`               Smart cache interface
  `ai_retry.c`                   AI retry engine
  `ai_retry.h`                   Retry configuration
  `autoexec.c`                   AI command execution queue
  `autoexec.h`                   Auto execution declarations
  `policy_dao.c`                 Policy database access
  `policy_dao.h`                 Policy DAO interface
  `policy_manager_gui.c`         Policy Manager
  `policy_manager_gui.h`         Policy Manager declarations
  `tee_handler.c`                Terminal capture and AI feed
  `tee_handler.h`                Tee declarations
  `noisefilter.c`                Noise filtering engine
  `noisefilter.h`                Noise filter interface
  `noise_filter_manager_gui.c`   Noise filter GUI
  `noise_filter_manager_gui.h`   Noise filter GUI declarations
  `ratelimit.c`                  Request limiter
  `ratelimit.h`                  Rate limiter declarations
  `snmp_manager.c`               SNMP engine
  `snmp_manager.h`               SNMP declarations
  `snmp_manager_gui.c`           SNMP Target Manager
  `snmp_manager_gui.h`           SNMP GUI declarations
  `session_manager.c`            Session persistence
  `session_manager.h`            Session declarations
  `session_manager_gui.c`        Session Manager
  `session_manager_gui.h`        Session GUI declarations
  `history_manager_gui.c`        History Manager
  `history_manager_gui.h`        History GUI declarations
  `export.c`                     Session export
  `export.h`                     Export declarations
  `print.c`                      GTK printing
  `print.h`                      Printing declarations
  `crypto.c`                     Credential encryption/decryption
  `crypto.h`                     Crypto declarations
  `config.c`                     Configuration load/save
  `config.h`                     Configuration declarations
  `idle.c`                       Idle watchdog and temporary toggle suspension
  `idle.h`                       Idle subsystem interface
  `status.c`                     Runtime status reporting
  `status.h`                     Status declarations
  `update.c`                     Runtime/AI update processing
  `update.h`                     Update declarations
  `utils.c`                      Shared utilities and DB initialization
  `utils.h`                      Utility declarations
  `xml_tagging.c`                AI payload tagging
  `xml_tagging.h`                XML tagging declarations
  `help.c`                       Built-in help/features text
  `help.h`                       Help declarations
  `resources.xml`                GTK resource definition
  `aiterm-icon.png`              Application icon
  `Makefile`                     Primary build
  `Makefile.debian`              Debian-oriented build file
  `Makefile.opensuse`            openSUSE-oriented build file
  `mkpkg.sh`                     Packaging helper
  `show-source.sh`               Source display helper
  `aiterm-test.py`               Test/helper script
  `aiterm-test2.py`              Test/helper script

------------------------------------------------------------------------

# Testing and Diagnostics

A practical development test sequence is:

``` bash
make clean
make -j"$(nproc)"
```

Then:

``` bash
./aiterm --version
```

Start with debug output:

``` bash
./aiterm --debug
```

Test provider configuration:

``` text
/open provider manager
```

Test database-backed session functions:

``` text
/session new
/session show
/session list
```

Test Tee:

``` text
/tee on
/tee status
```

Test Auto Reply:

``` text
/autoreply on
/autoreply status
```

Test SNMP:

``` text
/open snmp manager
/snmp cmd list
/snmp ticker status
/snmp enable status
```

Test idle suspension using:

``` ini
idle_timeout_minutes=5
```

Then leave the application untouched for five minutes.

The expected test behavior is:

``` text
Before idle:
    Enabled toggles remain enabled.

After idle:
    Managed toggles become OFF.
    SNMP ticker timer is stopped.
    Application remains open.

After user activity:
    Previous toggle states return.
    SNMP ticker resumes if it was previously enabled.
```

------------------------------------------------------------------------

# Security Notes

## Protect `/etc/aiterm.conf`

The file contains encrypted credentials, but encryption is not a
substitute for filesystem permissions.

Recommended:

``` bash
sudo chmod 600 /etc/aiterm.conf
```

and ownership should be appropriate for the account running aiterm.

## Protect the master key

Avoid:

``` bash
history
```

entries containing the master key where possible.

Instead of:

``` bash
./aiterm --master='secret'
```

consider:

``` bash
export AITERM_MASTER_KEY='secret'
./aiterm
unset AITERM_MASTER_KEY
```

Shell history behavior varies by shell and configuration, so use the
approach appropriate for your environment.

## Auto Execute

Auto Execute can cause commands generated by an AI system to enter a
real shell execution workflow.

Treat this as a privileged feature.

Use the Policy Manager and confirmation workflow rather than blindly
enabling automatic execution for an environment where destructive
commands are possible.

## SNMP community strings

SNMP configuration can contain community strings. Treat them as
credentials and restrict database access accordingly.

## Decrypt command

Never run:

``` bash
./aiterm --decrypt-pw
```

in a shared terminal, terminal recording session, CI log, or screenshot
workflow.

------------------------------------------------------------------------

# Known Limitations and Source-Tree Caveats

This README intentionally documents the source tree as it exists rather
than claiming functionality that is not present.

## `sessions` schema

The source migrates the `sessions` table but does not create it in the
visible database initialization sequence. A compatible existing table is
therefore required for full session functionality.

## `snmp_targets` schema

The SNMP manager and command handlers use the `snmp_targets` table, but
the visible initialization code does not create that table. A compatible
schema must exist.

## No bundled `schema.sql`

The current source tree does not contain a `schema.sql` file despite
older README text referring to one.

That older documentation should not be followed literally.

## Distro-specific Makefiles

The primary `Makefile` contains the most complete object list.

The included `Makefile.debian` and `Makefile.opensuse` are useful
references but do not currently mirror the complete object list of the
primary Makefile. If they fail to link, use the primary `Makefile`
first.

## Idle timeout zero

The configuration loader accepts zero, and the watchdog contains a
zero-disabled branch, but `idle_init()` currently replaces zero with the
default timeout.

Therefore the current effective behavior is:

``` text
idle_timeout_minutes > 0
    configured timeout

idle_timeout_minutes = 0
    replaced with default timeout
```

This should be corrected in a future cleanup if zero is intended to mean
"disabled."

## `Auto All`

`TOGGLE_AUTO_ALL` exists in the toggle enumeration and `/auto all`
exists in the command registry, but Auto All is not currently a separate
checkbox in the GUI Toggles menu.

## Build verification

The source tree should be built on the target Linux system because the
required GTK/VTE/MariaDB/Net-SNMP development environment is external to
the repository.

------------------------------------------------------------------------

# Recommended First-Run Procedure

For a new Linux installation:

### 1. Install development dependencies

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

### 2. Build

``` bash
make clean
make -j"$(nproc)"
```

### 3. Create the configuration

``` bash
sudo cp aiterm.conf.example /etc/aiterm.conf
sudo chmod 600 /etc/aiterm.conf
```

### 4. Generate encrypted credentials

``` bash
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='YOUR_API_KEY'
```

and:

``` bash
./aiterm --master='YOUR_MASTER_KEY' --crypt-pw='YOUR_DB_PASSWORD'
```

### 5. Configure the provider

For example:

``` ini
provider=gemini
model=gemini-flash-latest
api_key=<encrypted API key>
```

### 6. Configure MariaDB

``` ini
db_host=127.0.0.1
db_user=aiterm
db_pass=<encrypted DB password>
db_name=aiterm_db
```

Make sure the required application tables exist.

### 7. Configure idle protection

For the current test build:

``` ini
idle_timeout_minutes=5
```

### 8. Start aiterm

``` bash
./aiterm --master='YOUR_MASTER_KEY'
```

or:

``` bash
export AITERM_MASTER_KEY='YOUR_MASTER_KEY'
./aiterm
```

### 9. Verify the provider

Open:

``` text
Managers → AI Provider Manager
```

and use its connection test.

### 10. Verify the database

Run:

``` text
/session new
/session show
/session list
```

### 11. Verify the runtime toggles

Run:

``` text
/tee status
/autoreply status
/autoexe status
/snmp enable status
/snmp ticker status
/autoretry status
/ratelimit status
```

### 12. Test idle mode

Enable the features you want to test, then leave aiterm untouched for
five minutes.

Return to the application and perform a keyboard or mouse action.

The previous runtime toggle states should return.

------------------------------------------------------------------------

# Development Philosophy

aiterm has grown as a practical systems tool rather than as a minimal
terminal emulator.

The source therefore contains several overlapping layers:

``` text
Terminal
    +
AI
    +
Persistent Context
    +
Telemetry
    +
Automation
    +
Safety Controls
    +
Runtime Controls
```

The most important design boundary is that these layers remain
individually controllable.

The Toggles system, Provider Manager, Policy Manager, Noise Filter
Manager, Session Manager, SNMP Manager, rate limiter, retry subsystem,
and idle watchdog are all intended to let the operator decide how much
automation is active at any given moment.

------------------------------------------------------------------------

# License

See:

``` text
LICENSE
```

for the license applicable to this project.

------------------------------------------------------------------------

## Project Status

This README documents the source tree reviewed for the **0.9.10-beta
idle test build**.

It supersedes the older README content that referenced earlier
0.8.x/0.9.x feature sets and obsolete setup instructions.

The source tree should be treated as the authoritative reference for
behavior when this document and older historical notes disagree.
