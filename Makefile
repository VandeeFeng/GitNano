CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
LDFLAGS = -lz -lssl -lcrypto

SRCDIR = src
TESTDIR = tests
BUILDDIR = build

# Source files
LIB_SOURCES = $(filter-out $(SRCDIR)/main.c,$(wildcard $(SRCDIR)/*.c))
LIB_SOURCES += $(wildcard $(SRCDIR)/core/*.c)
LIB_SOURCES += $(wildcard $(SRCDIR)/objects/*.c)
LIB_SOURCES += $(wildcard $(SRCDIR)/utils/*.c)

# Convert source paths to object paths
SRC_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(filter-out $(SRCDIR)/utils/%.c,$(filter-out $(SRCDIR)/core/%.c,$(filter-out $(SRCDIR)/objects/%.c,$(LIB_SOURCES)))))
CORE_OBJECTS = $(patsubst $(SRCDIR)/core/%.c,$(BUILDDIR)/core/%.o,$(filter $(SRCDIR)/core/%.c,$(LIB_SOURCES)))
OBJECTS_OBJECTS = $(patsubst $(SRCDIR)/objects/%.c,$(BUILDDIR)/objects/%.o,$(filter $(SRCDIR)/objects/%.c,$(LIB_SOURCES)))
UTILS_OBJECTS = $(patsubst $(SRCDIR)/utils/%.c,$(BUILDDIR)/utils/%.o,$(filter $(SRCDIR)/utils/%.c,$(LIB_SOURCES)))
LIB_OBJECTS = $(SRC_OBJECTS) $(CORE_OBJECTS) $(OBJECTS_OBJECTS) $(UTILS_OBJECTS)
MAIN_OBJECT = $(BUILDDIR)/main.o

# Target executable
TARGET = gitnano
TEST_TARGET = test_runner
STATIC_LIB = libgitnano.a

.PHONY: all clean test install

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)
	mkdir -p $(BUILDDIR)/core
	mkdir -p $(BUILDDIR)/objects
	mkdir -p $(BUILDDIR)/utils

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILDDIR)/core/%.o: $(SRCDIR)/core/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILDDIR)/objects/%.o: $(SRCDIR)/objects/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILDDIR)/utils/%.o: $(SRCDIR)/utils/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -I$(SRCDIR)/utils -c $< -o $@

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
	cp include/*.h /usr/local/include/gitnano/