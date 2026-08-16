# beatLock

A PAM authentication module that enhances password security by requiring passwords to be entered in a specific rhythm or typing pattern.

## Overview

beatLock adds an additional layer of security by analyzing **how** you type your password, instead of just **what** you type. It measures:

- Dwell time: How long each key is held down
- Flight time: Time between key releases and presses

This biometric authentication method makes passwords significantly harder to crack, even if the password itself is compromised.

## Features

- Rhythm-based authentication using keystroke dynamics
- Integrates seamlessly with `sudo` via PAM on Linux
- Per-user typing patterns stored securely
- 3 authentication attempts before lockour
- No additional dependencies beyond PAM
- Evolution to match changes in typing rhythm over time

## Installation

### Prerequisites

- Linux with PAM support
- GCC compiler
- Root/sudo access

### Quickstart

```bash
make all
sudo make install
sudo beatlock-setup <username>
```

### Build

```bash
make all
```

### Install

```bash
sudo make install
```

### Enable beatLock

```bash
sudo ./src/enable-beatlock.sh
```

This backs up `/etc/pam.d/sudo` and injects the beatLock module into the PAM stack.

### Disable beatLock

```bash
sudo ./src/disable-beatlock.sh
```

## Usage

After installing and enabling beatLock (done by default on install), your first command should be to setup beatLock:

```bash
sudo beatlock-setup <username>
```

Your pattern will be stored in `/etc/security/beatlock/<username>.pattern`. Authentication attempts will verify that your typing matches the recorded rhythm.

## How It Works

1. Recording: The `beatlock-setup` command lets you setup your password. You cannot use beatLock before completing this step
2. Storage: Timing data is stored per-user in a root-protected directory
3. Authentication: Sudo attempts are verified by beatLock against the stored pattern
4. Tolerance: As this is designed for humans, not machines, a small tolerance is permitted for variations (to account for human error)
5. Update: Pattern is updated to shift over time according to the user

## Security Considerations

- beatLock is a **supplementary** authentication method, not a replacement for a strong password
- Your typing pattern is unique; protect it like a password
- Stored patterns are secure if and only if the proper file permissions are maintained (these will be set up correctly on install)
- beatLock supports passwords with a maximum length of 255 characters
- after 3 failed attempts, beatLock by default falls back to the default PAM

## Development

### File Structure

```
src/
├── beatlock.c          # Main PAM module
├── beatlock-setup.c    # Setup script (becomes beatlock-setup)
├── enable-beatlock.sh  # Installation script
└── disable-beatlock.sh # Removal script
```

### Building with Debug Info

```bash
make clean
make debug
```

## Experimental

**WARNING: The following is experimental and may lead to a broken Linux. Proceed at your own risk and only if you have adequate knowledge of what the following steps entail.**

To make beatLock the only authentication open `/etc/pam.d/sudo` as root. Find the line that says `auth sufficient pam_beatlock.so` and change it to say `auth required pam_beatlock.so`. Test this in another terminal window (before exiting your editor), so you can still change it back if it does not work.

You can run `sudo ls -l /etc/pam.d` to see with other services on your Linux require authentication. You can also use beatLock for these by adding the `auth sufficient pam_beatlock.so` or `auth required pam_beatlock.so` line to the top of the corresponding configuration files. Again, test configurations in a new terminal window (before closing your editor), to minimize the risk of being locked out.

## Troubleshooting

On linux, you can use `journalctl` to check beatLock logs. The following errors may appear:

| Log / Issue | Meaning |
| ---- | ---- |
| "beatLock: could not get PAM username" | beatLock cannot determine which user is attempting to authenticate |
| "beatLock: invalid username" | the username found by beatLock cannot be used for security reasons |
| "beatLock: cannot stat <path>"<br>"beatLock: <path> is not a regular file"<br>"beatLock: <path> is not root-owned"<br>"beatLock: <path> must be mode 0600"<br>"beatLock: could not open pattern file: <path>" | there is an issue with the stored pattern file; beatLock cannot access it or verify its integrity |
| "beatLock: could not open <path> for writing"<br>"beatLock: fchmod failed: "<br>""beatLock: fsync failed: "<br>"beatLock: rename failed: "<br>"beatLock: pattern file not correctly secured" | beatLock could not (securely) update the pattern file |
| "beatLock: write to TTY failed: " | beatLock tried to write the prompt to TTY but failed |

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please open an issue or a pull request.
