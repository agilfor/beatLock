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
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

#define BASE_DIR "/etc/security/beatlock"
#define THRESHOLD 0.05
#define MAX_KEYS 255
#define test_bit(bit, array) ((array[(bit) / 8] >> ((bit) % 8)) & 1)

typedef struct {
    int key;
    double dwell;
    double flight;
} key_press;

int global_tty_fd = -1;
struct termios global_old_term;

void handle_sigint(int sig) {
	printf("\n\n[!] Setup interrupted by user. Restoring terminal...\n");

	if (global_tty_fd >= 0) {
		tcsetattr(global_tty_fd, TCSAFLUSH, &global_old_term);
		close(global_tty_fd);
	}

	exit(130);
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
		
        fd = temp_fd;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        break;
	}
	closedir(dp);
	return fd;
}

void parse_password(double raw[MAX_KEYS][3], key_press pw[MAX_KEYS], size_t len) {
	if (len == 0) {
		fprintf(stderr, "ERROR: Cannot parse empty password\n");
		return;
	}

	pw[0].key = (int) raw[0][0];
	pw[0].dwell = raw[0][2] - raw[0][1];
	pw[0].flight = 0.0;
	for (size_t i = 1; i < len; i++) {
		pw[i].key = (int) raw[i][0];
		pw[i].dwell = raw[i][2] - raw[i][1];
		pw[i].flight = raw[i][1] - raw[i-1][1];
	}
	pw[len-1].flight = 0.0;
}

int check_matching(key_press pw1[MAX_KEYS], key_press pw2[MAX_KEYS], size_t n) {
	for (int i = 0; i < n; i++) {
		if (
			pw1[i].key != pw2[i].key ||
			fabs(pw1[i].dwell - pw2[i].dwell) > THRESHOLD ||
			fabs(pw1[i].flight - pw2[i].flight) > THRESHOLD
		) return 0;
	}
	return 1;
}

static int ensure_base_dir(void) {
    if (mkdir(BASE_DIR, 0700) != 0 && errno != EEXIST) {
        perror("mkdir");
        return -1;
    }

    struct stat st;
    if (stat(BASE_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ERROR: %s is not a directory\n", BASE_DIR);
        return -1;
    }

    if ((st.st_mode & 0777) != 0700) {
        fprintf(stderr, "ERROR: %s must be mode 0700\n", BASE_DIR);
        return -1;
    }

    if (st.st_uid != 0) {
        fprintf(stderr, "ERROR: %s must be owned by root\n", BASE_DIR);
        return -1;
    }

    return 0;
}

static int safe_pattern_path(const char *username, char *buf, size_t buflen) {
    if (!username || !*username || strlen(username) > 64) {
        return -1;
    }

    if (strchr(username, '/') || strchr(username, '\\') || strchr(username, '.')) {
        return -1;
    }

    snprintf(buf, buflen, "%s/%s.pattern", BASE_DIR, username);
    return 0;
}

static int write_pattern_file(const char *username, key_press final_pattern[MAX_KEYS], size_t length) {
    char filepath[512];
    if (safe_pattern_path(username, filepath, sizeof(filepath)) != 0) {
        fprintf(stderr, "ERROR: invalid username\n");
        return -1;
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.pattern.tmp", BASE_DIR, username);

    FILE *fptr = fopen(tmp_path, "w");
    if (!fptr) {
        fprintf(stderr, "ERROR: Could not open %s for writing\n", tmp_path);
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
        perror("fchmod");
        fclose(fptr);
        unlink(tmp_path);
        return -1;
    }

    if (fsync(fileno(fptr)) != 0) {
        perror("fsync");
        fclose(fptr);
        unlink(tmp_path);
        return -1;
    }

    fclose(fptr);

    if (rename(tmp_path, filepath) != 0) {
        perror("rename");
        unlink(tmp_path);
        return -1;
    }

    struct stat st;
    if (stat(filepath, &st) != 0 || st.st_uid != 0 || (st.st_mode & 0777) != 0600) {
        fprintf(stderr, "ERROR: pattern file not correctly secured\n");
        unlink(filepath);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
	signal(SIGINT, handle_sigint);

    if (geteuid() != 0) {
        printf("ERROR: You must run this tool as root (sudo).\n");
        return 1;
    }

    if (argc != 2) {
        printf("Usage: sudo ./beatlock-enroll <username>\n");
        return 1;
    }

	if (ensure_base_dir() != 0) {
		return 1;
	}

    int kbd_fd = find_kbd_fd();
    if (kbd_fd == -1) {
        printf("ERROR: Could not locate a physical keyboard.\n");
        return 1;
    }

	global_tty_fd = open("/dev/tty", O_RDWR);
    struct  termios new_term;
    if (global_tty_fd >= 0) {
        tcgetattr(global_tty_fd, &global_old_term);
        new_term = global_old_term;
        new_term.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(global_tty_fd, TCSANOW, &new_term);
    }

    key_press attempts[3][MAX_KEYS];
    size_t lengths[3] = {0, 0, 0};
	int valid_attempts = 0;
	int fail_count = 0;

    printf("\n=== BeatLock Setup ===\n");
    printf("You will need to type your password (in rhythm) 3 times to establish a baseline.\n");

	for (int i = 0; i < 3; i++) {
        printf("\nAttempt %d of 3. Type password in rhythm and press ENTER: ", i + 1);
        fflush(stdout);

        struct input_event ev;
        double down_timestamps[KEY_MAX] = {0.0};
        double timestamps[MAX_KEYS][3] = {{0.0}};
        size_t n = 0;

        while (read(kbd_fd, &ev, sizeof(struct input_event)) > 0) {
            if (ev.type == EV_KEY) {
                double timestamp = (double)ev.time.tv_sec + (double)ev.time.tv_usec / 1000000.0;

                if (ev.value == 1) { // key down
                    if (ev.code == KEY_ENTER) break;
                    down_timestamps[ev.code] = timestamp;
                } else if (ev.value == 0) { // key up
                    if (ev.code == KEY_BACKSPACE) {
                        memset(down_timestamps, 0, sizeof(down_timestamps));
                        memset(timestamps, 0, sizeof(timestamps));
                        n = 0;
                        printf("\n[Resetting attempt. Start typing again]\n");
                    } else if (down_timestamps[ev.code] != 0.0) {
                        timestamps[n][0] = (double)ev.code;
                        timestamps[n][1] = down_timestamps[ev.code];
                        timestamps[n][2] = timestamp;
                        n++;
                        down_timestamps[ev.code] = 0.0;
                    }
                }
            }
        }

        if (n == 0) {
            printf("\nERROR: no keys were captured. Please try again.\n");
            i--;
            continue;
        }

        parse_password(timestamps, attempts[i], n);
        lengths[i] = n;

        if (i == 0) {
            valid_attempts++;
            continue;
        }

        if (lengths[0] != lengths[i] || check_matching(attempts[0], attempts[i], lengths[i]) == 0) {
			fail_count++;

			if (fail_count >= 3) {
				printf("\nERROR: Too many failed attempts. Restarting setup process...\n");
				i = -1;
				fail_count = 0;
				continue;
			}
	
            printf("\nERROR: please enter the same password as before.\n");
            i--;
            continue;
        }

        valid_attempts++;
    }


	close(kbd_fd);
	if (global_tty_fd >= 0) {
		tcsetattr(global_tty_fd, TCSAFLUSH, &global_old_term);
		close(global_tty_fd);
	}

	if (valid_attempts < 3) {
        fprintf(stderr, "ERROR: setup failed; pattern was not confirmed.\n");
        return 1;
    }

	key_press final_pattern[MAX_KEYS];
	size_t length = lengths[0];
	
	final_pattern[0].key = attempts[0][0].key;
	final_pattern[0].dwell = (attempts[0][0].dwell + attempts[1][0].dwell + attempts[2][0].dwell) / 3.0;
	final_pattern[0].flight = 0;
	for (size_t k = 1; k < length; k++) {
		final_pattern[k].key = attempts[0][k].key;
		final_pattern[k].dwell = (attempts[0][k].dwell + attempts[1][k].dwell + attempts[2][k].dwell) / 3.0;
		final_pattern[k].flight = (k == 0 || k == length - 1) ? 0.0 : (attempts[0][k].flight + attempts[1][k].flight + attempts[2][k].flight) / 3.0;
	}

	if (write_pattern_file(argv[1], final_pattern, length) != 0) {
		fprintf(stderr, "ERROR: could not save beatLock pattern\n");
		return 1;
	}

	printf("\nSUCCESS! BeatLock has been setup for user '%s'.\n", argv[1]);
	
	return 0;
}