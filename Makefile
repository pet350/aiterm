CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0 vte-2.91 libcurl json-c mariadb` -Wall -Wno-deprecated-declarations
# Example
LIBS=`pkg-config --libs gtk+-3.0 vte-2.91 mariadb` -ljson-c -lpthread -lcurl -lcrypto
# LIBS = $(shell pkg-config --libs gtk+-3.0 vte-2.91 libcurl json-c mariadb) -lpthread

# Centralize objects
OBJ = main.o gui.o terminal.o openai.o gemini.o update.o help.o menu.o utils.o tee_handler.o crypto.o resources.o policy_dao.o
TARGET = aiterm

# 1. The DEFAULT target (must be first)
all: $(TARGET)

# 2. Link the binary
$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIBS)

# 3. Rule for the generated resource source
# This MUST exist for resources.o to know how to be built
resources.c: resources.xml aiterm-icon.png
	glib-compile-resources resources.xml --target=resources.c --generate-source

# 4. Pattern rule for all other .c files
%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

# 5. Clean rule
clean:
	rm -fv *.o $(TARGET) resources.c

.PHONY: all clean
