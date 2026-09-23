CC ?= gcc

# Auto-detect MariaDB / MySQL pkg-config module name
MARIADB_PKG := $(shell pkg-config --exists libmariadb && echo libmariadb || (pkg-config --exists mariadb && echo mariadb || echo mysqlclient))

# Base dependencies
PKGS = gtk+-3.0 glib-2.0 vte-2.91 libcurl json-c $(MARIADB_PKG)

# --- Install locations ---
# PREFIX only affects where the *binary* goes (standard convention for a
# locally-built install, e.g. /usr/local/bin). DATADIR/SQLDIR default to
# /usr/share/aiterm/db independently of PREFIX, matching the absolute system
# paths this app already uses elsewhere (e.g. /etc/aiterm.conf in
# get_config_filename(), utils.c) rather than being PREFIX-relative.
# Override either independently if you need to, e.g.:
#   make install PREFIX=/opt/aiterm
#   make install DATADIR=/opt/aiterm/share
# DESTDIR is a staging root for packaging (rpm/deb buildroots) and must NOT
# leak into the path compiled into the binary - it's only used in the
# install/uninstall recipes below, never in CFLAGS.
PREFIX  ?= /usr/local
DATADIR ?= /usr/share
BINDIR   = $(PREFIX)/bin
SQLDIR   = $(DATADIR)/aiterm/db

CFLAGS += $(shell pkg-config --cflags $(PKGS)) -Wall -Wno-deprecated-declarations
# Bakes SQLDIR into the binary as the fallback init_db_from_directory() path
# (see get_sql_init_dir() / AITERM_SQL_DIR_DEFAULT in utils.c). Runtime
# override still works via the AITERM_SQL_DIR env var regardless of this.
CFLAGS += -DAITERM_SQL_DIR_DEFAULT='"$(SQLDIR)"'
LIBS   += $(shell pkg-config --libs $(PKGS)) -lpthread -lcurl -lcrypto -luuid -lnetsnmp

OBJ = ai_provider.o provider_manager_gui.o ai_retry.o autoexec.o commands.o config.o crypto.o export.o gemini.o gemini_cache.o gui.o help.o \
      history_manager_gui.o idle.o main.o menu.o noise_filter_manager_gui.o noisefilter.o openai.o \
      policy_dao.o policy_manager_gui.o print.o provider_keys.o ratelimit.o resources.o session_manager_gui.o \
      session_manager.o snmp_manager.o snmp_manager_gui.o status.o tee_handler.o terminal.o toggles.o \
      update.o utils.o xml_tagging.o 

TARGET = aiterm

# *.sql files to install, evaluated at parse time relative to this Makefile
SQL_FILES = $(wildcard db/*.sql)

.PHONY: all clean install install-bin install-db uninstall

# 1. The DEFAULT target
all: build_id.h $(TARGET)

# 2. Generate the build ID header
build_id.h:
	@echo "Generating Build ID..."
	@echo '#ifndef BUILD_ID_H'			> build_id.h
	@echo '#define BUILD_ID_H'			>> build_id.h
	@echo "#define BUILD_ID \"$$(uuidgen)\""	>> build_id.h
	@echo "#define BUILD_TIME \"$$(date)\""		>> build_id.h
	@echo "#endif"					>> build_id.h

# 3. Rule for GLib resources (Ensures target exists for compilation)
resources.c: resources.xml aiterm-icon.png
	glib-compile-resources resources.xml --target=resources.c --generate-source

# 4. Link the binary
$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIBS)

# 5. Pattern rule for .c files
%.o: %.c build_id.h
	$(CC) $(CFLAGS) -c -o $@ $<

# 6. Clean rule
clean:
	rm -fv *.o $(TARGET) resources.c build_id.h

# 7. Install the binary to $(DESTDIR)$(BINDIR)
install-bin: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

# 8. Install schema init scripts to $(DESTDIR)$(SQLDIR). This must line up
# with the AITERM_SQL_DIR_DEFAULT baked into the binary above - if you
# override SQLDIR here, override it consistently for the build too, or set
# AITERM_SQL_DIR at runtime to point at wherever these actually ended up.
install-db:
	@if [ -z "$(SQL_FILES)" ]; then \
		echo "WARNING: no *.sql files found in ./db - skipping schema install."; \
	else \
		install -d $(DESTDIR)$(SQLDIR); \
		install -m 0644 $(SQL_FILES) $(DESTDIR)$(SQLDIR)/; \
	fi

# 9. Full install: builds first, then installs binary + schema scripts
install: all install-bin install-db
	@echo "Installed $(TARGET) to $(DESTDIR)$(BINDIR)"
	@echo "Installed SQL init scripts to $(DESTDIR)$(SQLDIR)"

# 10. Reverses install: only removes what install would have put there,
# never rm -rf's the whole data dir in case something else was dropped in it
uninstall:
	rm -fv $(DESTDIR)$(BINDIR)/$(TARGET)
	@for f in $(SQL_FILES); do \
		rm -fv $(DESTDIR)$(SQLDIR)/$$(basename $$f); \
	done
	-rmdir $(DESTDIR)$(SQLDIR) 2>/dev/null
	-rmdir $(DESTDIR)$(DATADIR)/aiterm 2>/dev/null

