CC ?= gcc

# Auto-detect MariaDB / MySQL pkg-config module name
MARIADB_PKG := $(shell pkg-config --exists libmariadb && echo libmariadb || (pkg-config --exists mariadb && echo mariadb || echo mysqlclient))

# Base dependencies
PKGS = gtk+-3.0 glib-2.0 vte-2.91 libcurl json-c $(MARIADB_PKG)

CFLAGS += $(shell pkg-config --cflags $(PKGS)) -Wall -Wno-deprecated-declarations
LIBS   += $(shell pkg-config --libs $(PKGS)) -lpthread -lcurl -lcrypto -luuid -lnetsnmp

OBJ = ai_provider.o provider_manager_gui.o ai_retry.o autoexec.o commands.o config.o crypto.o export.o gemini.o gemini_cache.o gui.o help.o \
      history_manager_gui.o idle.o main.o menu.o noise_filter_manager_gui.o noisefilter.o openai.o \
      policy_dao.o policy_manager_gui.o print.o ratelimit.o resources.o session_manager_gui.o \
      session_manager.o snmp_manager.o snmp_manager_gui.o status.o tee_handler.o terminal.o toggles.o \
      update.o utils.o xml_tagging.o 

TARGET = aiterm

.PHONY: all clean

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

