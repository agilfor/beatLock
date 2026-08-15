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

#define BASE_DIR "/etc/security/beatlock"
#define MAX_KEYS 255
#define test_bit(bit, array) ((array[(bit) / 8] >> ((bit) % 8)) & 1)

typedef struct {
    int key;
    double dwell;
    double flight;
} key_press;

double get_time_in_seconds(struct timespec *ts) {
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

int find_kbd_fd() {
	struct dirent *entry;
	DIR *dp = opendir("/dev/input");
	if (dp == NULL) return -1;
	int fd = -1;
	char path[256];
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
		if (test_bit(EV_KEY, ev_bits)) {
			ioctl(temp_fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
			if (test_bit(KEY_A, key_bits) && test_bit(KEY_ENTER, key_bits)) {
				fd = temp_fd;
				int flags = fcntl(fd, F_GETFL, 0);
				fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
				break;
			}
		}
		close(temp_fd);
	}
	closedir(dp);
	return fd;
}

void parse_password(double raw[MAX_KEYS][3], key_press pw[MAX_KEYS]) {
	pw[0].key = (int) raw[0][0];
	pw[0].dwell = raw[0][2] - raw[0][1];
	pw[0].flight = 0.0;
	for (int i = 1; i < MAX_KEYS; i++) {
		if (raw[i][1] == 0.0) {
			pw[i-1].flight = 0.0;
			break;
		}
		pw[i].key = (int) raw[i][0];
		pw[i].dwell = raw[i][2] - raw[i][1];
		pw[i].flight = raw[i][1] - raw[i-1][1];
	}
}

int check_matching(key_press pw1[MAX_KEYS], key_press pw2[MAX_KEYS], size_t n) {
	for (int i = 0; i < n; i++) {
		if (pw1[i].key != pw2[i].key) return 0;
	}
	return 1;
}

int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        printf("ERROR: You must run this tool as root (sudo).\n");
        return 1;
    }

    if (argc != 2) {
        printf("Usage: sudo ./beatlock-enroll <username>\n");
        return 1;
    }

    mkdir(BASE_DIR, 0700);

    int kbd_fd = find_kbd_fd();
    if (kbd_fd == -1) {
        printf("ERROR: Could not locate a physical keyboard.\n");
        return 1;
    }

    int tty_fd = open("/dev/tty", O_RDWR);
    struct  termios old_term, new_term;
    if (tty_fd >= 0) {
        tcgetattr(tty_fd, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(tty_fd, TCSANOW, &new_term);
    }

    key_press attempts[3][MAX_KEYS];
    size_t length = 0;

    printf("\n=== BeatLock Setup ===\n");
    printf("You will need to type your password (in rhythm) 3 times to establish a baseline.\n");

	for (int i = 0; i < 3; i++) {
		printf("\nAttempt %d of 3. Type password in rhythm and press ENTER: ", i + 1);
		fflush(stdout);

		struct input_event ev;
		double down_timestamps[KEY_MAX] = {0.0};
		double timestamps[MAX_KEYS][3] = {0.0};
		size_t n = 0;

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
						printf("\n[Resetting attempt. Start typing again]\n");
					}
					else if (down_timestamps[ev.code] != 0.0) {
						timestamps[n][0] = (double) ev.code;
						timestamps[n][1] = down_timestamps[ev.code];
						timestamps[n][2] = timestamp;
						n++;
						down_timestamps[ev.code] = 0.0;
					}
				}
			}
		}

		parse_password(timestamps, attempts[i]);
		if (length == 0) length = n;
		else if (length != n) {
			printf("\nERROR: please enter the same password as before (or Ctrl+C to exit setup)\n");
			memset(attempts[i], 0, sizeof(attempts[i]));
			i--;
		} else if (check_matching(attempts[0], attempts[i]) == 0) {
			printf("\nERROR: please enter the same password as before (or Ctrl+C to exit setup)\n");
			memset(attempts[i], 0, sizeof(attempts[i]));
			i--;
		}
		printf("\n[Recorded %zu keys]", n);
	}

	close(kbd_fd);
	if (tty_fd >= 0) {
		tcsetattr(tty_fd, TCSAFLUSH, &old_term);
		close(tty_fd);
	}

	key_press final_pattern[MAX_KEYS];
	
	final_pattern[0].key = attempts[0][0].key;
	final_pattern[0].dwell = (attempts[0][0].dwell + attempts[1][0].dwell + attempts[2][0].dwell) / 3.0;
	final_pattern[0].flight = 0;
	for (size_t k = 1; k < length; k++) {
		final_pattern[k].key = attempts[0][k].key;
		final_pattern[k].dwell = (attempts[0][k].dwell + attempts[1][k].dwell + attempts[2][k].dwell) / 3.0;
		final_pattern[k].flight = (attempts[0][k].flight + attempts[1][k].flight + attempts[2][k].flight) / 3.0;
	}
	final_pattern[length-1].flight = 0;

	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/%s.pattern", BASE_DIR, argv[1]);

	FILE *fptr = fopen(filepath, "w");
	if (fptr == NULL) {
		printf("\nERROR: Could not write to %s\n", filepath);
		return 1;
	}

	for (size_t i = 0; i < length; i++) {
		fprintf("%d %lf %lf\n", final_pattern[i].key, final_pattern[i].dwell, final_pattern[i].flight);
	}
	fclose(fptr);
	chmod(filepath, 0600);

	printf("\nSUCCESS! BeatLock has been setup for user '%s'.\n", argv[1]);
	
	return 0;
}