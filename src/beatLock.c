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

#define PATH "./pattern"
#define MAX_KEYS 255
#define TOLERANCE 0.15
#define test_bit(bit, array)  ((array[(bit) / 8] >> ((bit) % 8)) & 1)

// temporary to suppress Mac errors
// #define EV_MAX 8
// #define KEY_MAX 8

typedef struct {
	int key;
	double dwell;
	double flight;
} key_press;

void load_pattern(key_press target_pattern[MAX_KEYS], size_t *pattern_size) {
	*pattern_size = 0;
	if (access(PATH, F_OK) == 0) {
		FILE *fptr;
		fptr = fopen(PATH, "r");
		size_t n = 0;
		int c;
		double dwell, flight;
		while (n < MAX_KEYS && fscanf(fptr, "%d %lf %lf\n", &c, &dwell, &flight) == 3) {
			target_pattern[n].key = c;
			target_pattern[n].dwell = dwell;
			target_pattern[n].flight = flight;
			n++;
		}
		*pattern_size = n;
		fclose(fptr);
	} else {
		// file doesn't exist
	}
}

void store_pattern(key_press pattern[MAX_KEYS], size_t *pattern_size) {
	FILE *fptr;
	fptr = fopen(PATH, "w");
	for (size_t i = 0; i < *pattern_size; i++) {
		fprintf(fptr, "%d %lf %lf\n", pattern[i].key, pattern[i].dwell, pattern[i].flight);
	}
	fclose(fptr);
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
				printf("Found valid keyboard at: %s\n", path);
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
		printf("%d %lf %lf", pw[i].key, pw[i].dwell, pw[i].flight);
		pw[i].key = (int) raw[i][0];
		pw[i].dwell = raw[i][2] - raw[i][1];
		pw[i].flight = raw[i][1] - raw[i-1][1];
	}
}

int check_password(key_press expected[MAX_KEYS], key_press recorded[MAX_KEYS], size_t expected_size, size_t recorded_size) {
	if (expected_size != recorded_size) {
		printf("ERROR: Expected and recorded sizes do not match.\n");
		return 0;
	}
	
	double total_deviation = 0.0;
	for (int i = 0; i < recorded_size; i++) {
		if (expected[i].key != recorded[i].key) {
			printf("ERROR: Incorrect password.\n");
			return 0;
		}
		total_deviation += fabs(expected[i].dwell - recorded[i].dwell);
		total_deviation += fabs(expected[i].flight - recorded[i].flight);
	}
	total_deviation /= recorded_size * 2;
	printf("Deviation: %lf; Tolerance: %lf\n", total_deviation, TOLERANCE);
	if (total_deviation > TOLERANCE) {
		printf("ERROR: Incorrect typing pattern.\n");
		return 0;
	}
	for (int i = 0; i < recorded_size; i++) {
		expected[i].dwell = (recorded[i].dwell + (expected[i].dwell * 3)) / 4;
		expected[i].flight = (recorded[i].flight + (expected[i].flight * 3)) / 4;
	}
	return 1;
}

double get_time_in_seconds(struct timespec *ts) {
	return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

int main() {
	int kbd_fd = find_kbd_fd();

	if (kbd_fd == -1) {
		printf("Could not locate input source. Are you root?\n");
		return 1;
	}

	struct input_event ev;
	double down_timestamps[KEY_MAX] = {0.0};
	double timestamps[MAX_KEYS][3] = {0.0};
	key_press recorded[MAX_KEYS];
	size_t n = 0;
	key_press expected[MAX_KEYS];
	size_t expected_size;
	load_pattern(expected, &expected_size);

	while (read(kbd_fd, &ev, sizeof(struct input_event)) > 0) {
		if (ev.type == EV_KEY) {
			double timestamp = (double)ev.time.tv_sec + (double)ev.time.tv_usec / 1000000.0;
			if (ev.value == 1) { // key down
				down_timestamps[ev.code] = timestamp;
			}
			else if (ev.value == 0) { // key up
				if (ev.code == KEY_ENTER) break;
				else if (ev.code == KEY_BACKSPACE) {
					memset(down_timestamps, 0, sizeof(down_timestamps));
					memset(timestamps, 0, sizeof(timestamps));
					memset(recorded, 0, sizeof(recorded));
					n = 0;
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

	parse_password(timestamps, recorded);
	if (expected_size == 0) {
		store_pattern(recorded, &n);
		printf("Successfully recorded password.\n");
	} else {
		int valid = check_password(expected, recorded, expected_size, n);
		printf("%d", valid);
		if (valid == 1) {
			printf("ACCESS GRANTED");
			return 0;
		} else {
			printf("ACCESS DENIED\n");
			return 1;
		}
	}
}