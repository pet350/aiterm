CC = gcc
# Fetch flags for all three major libraries

#CFLAGS = `pkg-config --cflags gtk+-3.0 vte-2.91 libcurl json-c` -Wall

CFLAGS = `pkg-config --cflags gtk+-3.0 vte-2.91 libcurl json-c mariadb` -Wall -Wno-deprecated-declarations
LIBS   = `pkg-config --libs gtk+-3.0 vte-2.91 libcurl json-c mariadb`

# CFLAGS = `pkg-config --cflags gtk+-3.0 vte-2.91 libcurl` -Wall
# LIBS = `pkg-config --libs gtk+-3.0 vte-2.91 libcurl`

# List of object files based on your modular structure
OBJ = main.o gui.o terminal.o openai.o gemini.o update.o help.o menu.o utils.o

# The final executable name
TARGET = aiterm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIBS)

# Pattern rule to compile .c to .o
%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -fv *.o $(TARGET)

.PHONY: all clean
