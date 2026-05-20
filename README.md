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
