CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
LDFLAGS = -lz -lssl -lcrypto

SRCDIR = src
TESTDIR = tests
BUILDDIR = build

# Source files
LIB_SOURCES = $(filter-out $(SRCDIR)/main.c,$(wildcard $(SRCDIR)/*.c))
LIB_OBJECTS = $(LIB_SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJECT = $(BUILDDIR)/main.o

# Target executable
TARGET = gitnano
TEST_TARGET = test_runner
STATIC_LIB = libgitnano.a

.PHONY: all clean test install

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build static library
$(STATIC_LIB): $(LIB_OBJECTS)
	ar rcs $@ $(LIB_OBJECTS)

# Build main executable
$(TARGET): $(LIB_OBJECTS) $(MAIN_OBJECT)
	$(CC) $(LIB_OBJECTS) $(MAIN_OBJECT) $(LDFLAGS) -o $@

# Test runner uses library objects
$(TEST_TARGET): $(LIB_OBJECTS) test_runner.c
	$(CC) $(CFLAGS) $(LIB_OBJECTS) test_runner.c $(LDFLAGS) -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_TARGET)

# Default install location
PREFIX = ~/.local
BINDIR = $(PREFIX)/bin

install: $(TARGET)
	cp $(TARGET) $(BINDIR)/

# Debug target
debug: CFLAGS += -DDEBUG -g3
debug: $(TARGET)

# Static library for embedding (alternative target)
static-lib: $(STATIC_LIB)

# Header only installation
install-headers:
	mkdir -p /usr/local/include/gitnano
	cp $(SRCDIR)/*.h /usr/local/include/gitnano/