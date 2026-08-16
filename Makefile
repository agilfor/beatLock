# --- Compiler and Flags ---
CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2
LDFLAGS_MATH = -lm
LDFLAGS_PAM = -lpam

# --- Targets ---
PAM_MODULE = pam_beatlock.so
SETUP_TOOL = beatlock-setup

# --- Installation Directories ---
PAM_DIR = /usr/lib/security
BIN_DIR = /usr/local/bin
CONFIG_DIR = /etc/security/beatlock

# --- Detect root for install ---
INSTALL_USER ?= root
INSTALL_GROUP ?= root

# --- Build Rules ---
.PHONY: all install uninstall clean check-deps help debug

all: check-deps $(PAM_MODULE) $(SETUP_TOOL)

$(PAM_MODULE): src/beatlock.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(LDFLAGS_PAM) $(LDFLAGS_MATH)
	@echo "[✓] Built PAM module: $@"

$(SETUP_TOOL): src/beatlock-setup.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_MATH)
	@chmod 750 $@
	@echo "[✓] Built setup tool: $@"

# --- Dependency Checking ---
check-deps:
	@echo "[*] Checking dependencies..."
	@if ! command -v gcc >/dev/null 2>&1; then \
		echo "[!] ERROR: gcc not found"; \
		exit 1; \
	fi
	@if ! command -v pkg-config >/dev/null 2>&1; then \
		echo "[!] ERROR: pkg-config not found"; \
		exit 1; \
	fi
	@if ! pkg-config --exists libpam; then \
		echo "[!] ERROR: libpam not found"; \
		echo ""; \
		echo "Install on Ubuntu/Debian: sudo apt-get install libpam0g-dev"; \
		echo "Install on Arch:          sudo pacman -S pam"; \
		echo "Install on Fedora/RHEL:   sudo dnf install pam-devel"; \
		echo "Install on Alpine:        sudo apk add linux-pam-dev"; \
		exit 1; \
	fi
	@if ! command -v ld >/dev/null 2>&1; then \
		echo "[!] ERROR: binutils (ld) not found"; \
		exit 1; \
	fi
	@echo "[✓] All dependencies found"

# --- Install ---
install: all
	@if [ "$$(id -u)" != "0" ]; then \
		echo "[!] ERROR: install requires root privileges"; \
		echo "    Run: sudo make install"; \
		exit 1; \
	fi
	@echo "[*] Installing beatLock..."
	
	# Create config directory with secure permissions
	@mkdir -p -m 700 $(CONFIG_DIR)
	@chown $(INSTALL_USER):$(INSTALL_GROUP) $(CONFIG_DIR)
	@echo "[✓] Created config directory: $(CONFIG_DIR)"
	
	# Install PAM module
	@install -D -m 644 $(PAM_MODULE) $(PAM_DIR)/$(PAM_MODULE)
	@chown $(INSTALL_USER):$(INSTALL_GROUP) $(PAM_DIR)/$(PAM_MODULE)
	@echo "[✓] Installed PAM module: $(PAM_DIR)/$(PAM_MODULE)"
	
	# Install setup tool with restricted permissions
	@install -D -m 4755 -o $(INSTALL_USER) -g $(INSTALL_GROUP) $(SETUP_TOOL) $(BIN_DIR)/$(SETUP_TOOL)
	@echo "[✓] Installed setup tool: $(BIN_DIR)/$(SETUP_TOOL) (setuid)"
	
	@echo ""
	@echo "[✓] Installation complete!"
	@echo "    Next step: sudo $(BIN_DIR)/$(SETUP_TOOL) <username>"

# --- Secure Uninstall ---
uninstall:
	@if [ "$$(id -u)" != "0" ]; then \
		echo "[!] ERROR: uninstall requires root privileges"; \
		exit 1; \
	fi
	@echo "[*] Uninstalling beatLock..."
	@rm -f $(PAM_DIR)/$(PAM_MODULE)
	@rm -f $(BIN_DIR)/$(SETUP_TOOL)
	@rm -rf $(CONFIG_DIR)
	@echo "[✓] Uninstall complete!"

# --- Clean Build Artifacts ---
clean:
	@echo "[*] Cleaning build artifacts..."
	@rm -f $(PAM_MODULE) $(SETUP_TOOL)
	@echo "[✓] Clean complete!"

# --- Debug Build ---
debug: CFLAGS = -Wall -Wextra -Werror -g -O0 -fstack-protector-all -D_FORTIFY_SOURCE=2
debug: clean all
	@echo "[✓] Debug build complete (with symbols)"

# --- Help ---
help:
	@echo "beatLock Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make all          - Build PAM module and setup tool"
	@echo "  make install      - Install (requires root)"
	@echo "  make uninstall    - Remove installation"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make debug        - Build with debug symbols"
	@echo "  make check-deps   - Verify dependencies"
	@echo "  make help         - Show this message"