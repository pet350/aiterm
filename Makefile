## Part of aiterm
## Makefile
CC = gcc

CFLAGS = `pkg-config --cflags gtk+-3.0 vte-2.91 libcurl json-c mariadb` -Wall -Wno-deprecated-declarations

LIBS = `pkg-config --libs gtk+-3.0 vte-2.91 mariadb` -ljson-c -lpthread -lcurl -lcrypto -luuid

OBJ = commands.o config.o crypto.o gemini.o gui.o help.o history_manager_gui.o main.o menu.o		\
      noise_filter_manager_gui.o noisefilter.o openai.o policy_dao.o policy_manager_gui.o 		\
      ratelimit.o resources.o session_manager_gui.o session_manager.o status.o tee_handler.o		\
      terminal.o toggles.o update.o utils.o

TARGET = aiterm

# 1. The DEFAULT target
all: build_id.h $(TARGET)

# 2. Generate the build ID header
build_id.h:
	@echo "Generating Build ID..."
	@echo '#ifndef BUILD_ID_H'			> build_id.h
	@echo '#define BUILD_ID_H'			>>build_id.h
	@echo "#define BUILD_ID \"$$(uuidgen)\""	>>build_id.h
	@echo "#define BUILD_TIME \"$$(date)\""		>>build_id.h
	@echo "#endif"					>>build_id.h

# 3. Link the binary
$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIBS)

# 4. Rule for resources (Ensures it happens before build)
resources.c: resources.xml aiterm-icon.png
	glib-compile-resources resources.xml --target=resources.c --generate-source

# 5. Pattern rule for .c files
%.o: %.c build_id.h
	$(CC) -c -o $@ $< $(CFLAGS)

# 6. Clean rule
clean:
	rm -fv *.o $(TARGET) resources.c build_id.h
