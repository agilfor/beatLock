#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <syslog.h>
#include <sys/stat.h>
#include <errno.h>

#define PAM_SM_AUTH
#include <security/pam_appl.h>
#include <security/pam_modules.h>

#define BASE_DIR "/etc/security/beatlock"
#define MAX_KEYS 255
#define TOLERANCE 0.05
#define test_bit(bit, array)  ((array[(bit) / 8] >> ((bit) % 8)) & 1)

typedef struct {
	int key;
	double dwell;
	double flight;
} key_press;

static int pattern_path_for_user(pam_handle_t *pamh, char *buf, size_t buflen) {
	const char *username = NULL;
    if (pam_get_user(pamh, &username, NULL) != PAM_SUCCESS || username == NULL || *username == '\0') {
		syslog(LOG_ERR, "beatLock: could not get PAM username");
		return -1;
	}

	if (strchr(username, '/') != NULL || strchr(username, '\\') != NULL || strlen(username) > 64) {
		syslog(LOG_ERR, "beatLock: invalid username");
		return -1;
	} 

	snprintf(buf, buflen, "%s/%s.pattern", BASE_DIR, username);
	return 0;
}

static int validate_pattern_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        syslog(LOG_ERR, "beatLock: cannot stat %s", path);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        syslog(LOG_ERR, "beatLock: %s is not a regular file", path);
        return -1;
    }

    if (st.st_uid != 0) {
        syslog(LOG_ERR, "beatLock: %s is not root-owned", path);
        return -1;
    }

    if ((st.st_mode & 0777) != 0600) {
        syslog(LOG_ERR, "beatLock: %s must be mode 0600", path);
        return -1;
    }

    return 0;
}

void load_pattern(pam_handle_t *pamh, key_press target_pattern[MAX_KEYS], size_t *pattern_size) {
	*pattern_size = 0;

	char filepath[512];
	if (pattern_path_for_user(pamh, filepath, sizeof(filepath)) != 0) {
		return;
	}

	if (validate_pattern_file(filepath) != 0) {
    	return;
	}

	FILE *fptr = fopen(filepath, "r");
    if (!fptr) {
        syslog(LOG_ERR, "beatLock: could not open pattern file: %s", filepath);
        return;
    }

	size_t n = 0;
	int c;
	double dwell, flight;
	while (n < MAX_KEYS && fscanf(fptr, "%d %lf %lf\n", &c, &dwell, &flight) == 3) {
		target_pattern[n].key = c;
		target_pattern[n].dwell = dwell;
		target_pattern[n].flight = flight;
		n++;
	}
	fclose(fptr);
	*pattern_size = n;
}

static int write_pattern_file(pam_handle_t *pamh, key_press final_pattern[MAX_KEYS], size_t length) {
    char filepath[512];
    
	if (pattern_path_for_user(pamh, filepath, sizeof(filepath)) != 0) {
        syslog(LOG_ERR, "beatLock: invalid username");
        return -1;
    }

	const char *username = NULL;
	pam_get_user(pamh, &username, NULL);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.pattern.tmp", BASE_DIR, username);

    FILE *fptr = fopen(tmp_path, "w");
    if (!fptr) {
        syslog(LOG_ERR, "beatLock: could not open %s for writing", tmp_path);
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        fprintf(fptr, "%d %lf %lf\n",
                final_pattern[i].key,
                final_pattern[i].dwell,
                final_pattern[i].flight);
    }

    fflush(fptr);
    if (fchmod(fileno(fptr), 0600) != 0) {
        syslog(LOG_ERR, "beatLock: fchmod failed: %s", strerror(errno));
        fclose(fptr);
        unlink(tmp_path);
        return -1;
    }

    if (fsync(fileno(fptr)) != 0) {
        syslog(LOG_ERR, "beatLock: fsync failed: %s", strerror(errno));
        fclose(fptr);
        unlink(tmp_path);
        return -1;
    }

    fclose(fptr);

    if (rename(tmp_path, filepath) != 0) {
        syslog(LOG_ERR, "beatLock: rename failed: %s", strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    struct stat st;
    if (stat(filepath, &st) != 0 || st.st_uid != 0 || (st.st_mode & 0777) != 0600) {
        syslog(LOG_ERR, "beatLock: pattern file not correctly secured\n");
        unlink(filepath);
        return -1;
    }

	syslog(LOG_INFO, "beatLock: successfully evolved typing pattern for user '%s'", username);

    return 0;
}

int find_kbd_fd() {
	struct dirent *entry;
	DIR *dp = opendir("/dev/input");
	if (dp == NULL) return -1;
	int fd = -1;
	char path[512];
	unsigned char ev_bits[EV_MAX / 8 + 1];
	unsigned char key_bits[KEY_MAX / 8 + 1];
	while ((entry = readdir(dp))) {
		if (strncmp(entry->d_name, "event", 5)) continue;

		snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
		int temp_fd = open(path, O_RDONLY | O_NONBLOCK);
		if (temp_fd < 0) continue;

		if (ioctl(temp_fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
			close(temp_fd);
			continue;
		}
		if (!test_bit(EV_KEY, ev_bits)) {
            close(temp_fd);
            continue;
        }
		
		ioctl(temp_fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
		
		if (!test_bit(KEY_A, key_bits) || !test_bit(KEY_ENTER, key_bits)) {
            close(temp_fd);
            continue;
        }

		if (test_bit(BTN_MOUSE, key_bits) || test_bit(BTN_LEFT, key_bits)) {
            close(temp_fd);
            continue;
        }
		
		syslog(LOG_INFO, "beatLock: found valid keyboard at: %s", path);
        fd = temp_fd;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        break;
	}
	closedir(dp);
	return fd;
}

int parse_password(double raw[MAX_KEYS][3], key_press pw[MAX_KEYS], size_t len) {
	if (len == 0 || len > MAX_KEYS) {
        syslog(LOG_ERR, "beatLock: invalid password length: %zu", len);
        return 1;
    }
	
	for (size_t i = 0; i < len; i++) {
		if (raw[i][1] > raw[i][2]) {
    		syslog(LOG_ERR, "beatLock: invalid timing data");
    		return 1;
		}
		pw[i].key = (int) raw[i][0];
		pw[i].dwell = raw[i][2] - raw[i][1];
		pw[i].flight = (i == 0) ? 0.0 : raw[i][1] - raw[i-1][1];
	}
	pw[len-1].flight = 0.0;
	return 0;
}

int check_password(key_press expected[MAX_KEYS], key_press recorded[MAX_KEYS], size_t expected_size, size_t recorded_size) {
	if (expected_size == 0 || recorded_size == 0) {
        syslog(LOG_ERR, "beatLock: empty password pattern");
        return 0;
    }

    if (expected_size != recorded_size) {
        syslog(LOG_ERR, "beatLock: expected and recorded sizes do not match.");
        return 0;
    }

    double total_deviation = 0.0;
    for (size_t i = 0; i < recorded_size; i++) {
        if (expected[i].key != recorded[i].key) {
            syslog(LOG_ERR, "beatLock: incorrect password.");
            return 0;
        }
        total_deviation += fabs(expected[i].dwell - recorded[i].dwell);
        total_deviation += fabs(expected[i].flight - recorded[i].flight);
    }

    total_deviation /= recorded_size * 2.0;
    if (total_deviation > TOLERANCE) {
        syslog(LOG_ERR, "beatLock: incorrect typing pattern.");
        return 0;
    }

    for (size_t i = 0; i < recorded_size; i++) {
        expected[i].dwell = (recorded[i].dwell + (expected[i].dwell * 3.0)) / 4.0;
        expected[i].flight = (recorded[i].flight + (expected[i].flight * 3.0)) / 4.0;
    }

    return 1;
}

void write_to_tty(int tty_fd, const char *message, ssize_t size) {
	int tty;
	if (tty_fd < 0) tty = open("/dev/tty", O_RDWR);
	else tty = tty_fd;
	ssize_t written =  write(tty, message, size);
	if (written < 0) {
		syslog(LOG_ERR, "beatLock: write to TTY failed: %s", strerror(errno));
	}
	if (tty_fd < 0) close(tty);
}

int perform_auth(pam_handle_t *pamh, int attempt) {
	if (attempt > 3) {
		syslog(LOG_ERR, "beatLock: maximum authentication attempts exceeded");
		return PAM_AUTH_ERR;
	}

	int kbd_fd = find_kbd_fd();

	if (kbd_fd == -1) {
		syslog(LOG_ERR, "beatLock: could not locate input source.");
		return PAM_AUTH_ERR;
	}

	key_press expected[MAX_KEYS];
	size_t expected_size;
	load_pattern(pamh, expected, &expected_size);

	if (expected_size == 0) {
	    syslog(LOG_ERR, "beatLock: no pattern loaded for user");
	    return PAM_AUTH_ERR;
	}

	struct input_event ev;
	double down_timestamps[KEY_MAX] = {0.0};
	double timestamps[MAX_KEYS][3] = {{0.0}};
	key_press recorded[MAX_KEYS];
	size_t n = 0;

	int tty_fd = open("/dev/tty", O_RDWR);
	struct termios old_term, new_term;
	if (tty_fd >= 0) {
		tcgetattr(tty_fd, &old_term);
		new_term = old_term;
		new_term.c_lflag &= ~(ECHO | ICANON);
		tcsetattr(tty_fd, TCSANOW, &new_term);
	}

	write_to_tty(tty_fd, "Password: ", 10);

	while (read(kbd_fd, &ev, sizeof(struct input_event)) > 0) {
		if (ev.type == EV_KEY) {
			double timestamp = (double)ev.time.tv_sec + (double)ev.time.tv_usec / 1000000.0;
			if (ev.value == 1) { // key down
				if (ev.code == KEY_ENTER) break;
				down_timestamps[ev.code] = timestamp;
			}
			else if (ev.value == 0) { // key up
				if (ev.code == KEY_BACKSPACE) {
					memset(down_timestamps, 0, sizeof(down_timestamps));
					memset(timestamps, 0, sizeof(timestamps));
					n = 0;
				}
				else if (down_timestamps[ev.code] != 0.0 && n < MAX_KEYS) {
					timestamps[n][0] = (double) ev.code;
					timestamps[n][1] = down_timestamps[ev.code];
					timestamps[n][2] = timestamp;
					n++;
					down_timestamps[ev.code] = 0.0;
				}
			}
		}
	}
	close(kbd_fd);
	write_to_tty(tty_fd, "\n", 1);

	if (tty_fd >= 0) {
		tcsetattr(tty_fd, TCSAFLUSH, &old_term);
		close(tty_fd);
	}

	if (n == 0) {
        syslog(LOG_ERR, "beatLock: empty or partial capture");
		write_to_tty(-1, "Incorrect password\n", 19);
        return perform_auth(pamh, attempt + 1);
    }

	if (parse_password(timestamps, recorded, n) != 0) {
		write_to_tty(-1, "Incorrect password\n", 19);
		return perform_auth(pamh, attempt + 1);
	}

	if (check_password(expected, recorded, expected_size, n) == 1) {
		write_pattern_file(pamh, expected, expected_size);
		return PAM_SUCCESS;
	}

	write_to_tty(-1, "Incorrect password\n", 19);
	return perform_auth(pamh, attempt + 1);
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv) {
	(void) flags;
	(void) argc;
	(void) argv;

	return perform_auth(pamh, 1);
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv) {
	(void) pamh;
	(void) flags;
	(void) argc;
	(void) argv;

	return PAM_SUCCESS;
}