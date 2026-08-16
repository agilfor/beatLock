#!/bin/bash

PAM_FILE="/etc/pam.d/sudo"
MODULE_LINE="auth sufficient pam_beatlock.so"

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Please run as root."
    exit 1
fi

if grep -q "pam_beatlock.so" "$PAM_FILE"; then
    echo "[*] BeatLock is already configured in $PAM_FILE. Skipping..."
    exit 0
fi

echo "[*] Backing up $PAM_FILE to ${PAM_FILE}.bak.."
cp "$PAM_FILE" "${PAM_FILE}.bak"

echo "[*] Injecting BeatLock into PAM stack..."

awk -v rule="$MODULE_LINE" '
    /^auth/ && !inserted {
        print rule
        inserted=1
    }
    { print }
' "${PAM_FILE}.bak" > "$PAM_FILE"

echo "[✓] BeatLock enabled for sudo!"