#!/bin/bash

PAM_FILE="/etc/pam.d/sudo"


if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Please run as root."
    exit 1
fi

echo "[*] Checking PAM stack in $PAM_FILE..."

if grep -q "pam_beatlock.so" "$PAM_FILE"; then
    echo "[*] Backing up $PAM_FILE to ${PAM_FILE}.bak.."
    cp "$PAM_FILE" "${PAM_FILE}.bak"

    echo "[*] Removing BeatLock from PAM stack..."

    sed -i '/pam_beatlock\.so/d' "$PAM_FILE"

    echo "[✓] BeatLock disabled for sudo!"
else
    echo "[*] BeatLock not found in PAM stack. Skipping..."
fi