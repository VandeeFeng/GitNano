# Simple Makefile wrapper for nob.h build system
# This forwards all make targets to the nob build system

.PHONY: all clean test install debug static-lib

# Default target - build the main executable
all:
	@./nob

# Build and run tests
test:
	@./nob test

# Clean build artifacts
clean:
	@./nob clean

# Debug build
debug:
	@./nob debug

# Build static library
static-lib:
	@./nob static-lib

# Install to ~/.local/bin
install:
	@./nob install

# Show help
help:
	@echo "GitNano Build System (nob.h)"
	@echo "==========================="
	@echo ""
	@echo "Available targets:"
	@echo "  make (or make all)  - Build GitNano"
	@echo "  make test           - Build and run tests"
	@echo "  make clean          - Clean build artifacts"
	@echo "  make debug          - Build with debug symbols"
	@echo "  make static-lib     - Build static library"
	@echo "  make install        - Install to ~/.local/bin"
	@echo "  make help           - Show this help"
	@echo ""
	@echo "You can also use the nob build system directly:"
	@echo "  ./nob               - Build GitNano"
	@echo "  ./nob test          - Build and run tests"
	@echo "  ./nob clean         - Clean build artifacts"
	@echo "  ./nob debug         - Build with debug symbols"
	@echo "  ./nob static-lib    - Build static library"
	@echo "  ./nob install       - Install to ~/.local/bin"