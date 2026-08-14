#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#define MAX_KEYS 255
#define TOLERANCE 0.15

double get_time_in_seconds(struct timespec *ts) {
	return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

int main() {
	struct termios old_term, new_term;
	struct timespec tap_times[MAX_KEYS];
	double deltas[MAX_KEYS - 1];
	char keys[MAX_KEYS];

	// this part needs to be wayyy better
	double target_deltas[] = {1.0, 1.0, 1.0};
	int target_length = 4;

	// terminal settings (to get direct input rather than a blob after enter)
	tcgetattr(STDIN_FILENO, &old_term);
	new_term = old_term;
	new_term.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
	
	printf("Tap your %d-key rhythm now...\n", target_length);

	for (int i = 0; i < target_length; i++) {
		keys[i] = getchar();
		clock_gettime(CLOCK_MONOTONIC, &tap_times[i]);
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
	
	printf("\nCaptured:\n");

	for (int i = 0; i < target_length-1; i++) {
		deltas[i] = get_time_in_seconds(&tap_times[i+1]) - get_time_in_seconds(&tap_times[i]);
		printf("Interval %d: %fs\n", i+1, deltas[i]);
	}

	int authenticated = 1;

	for (int i = 0; i < target_length - 1; i++) {
		double diff = fabs(deltas[i] - target_deltas[i]);
		if (diff > TOLERANCE) { 
			printf("Failed at interval %d (Off by %f sec)\n", i+1, diff);
			authenticated = 0;
			break;
		}
	}

	if (authenticated) {
		printf("ACCESS GRANTED. Good rhythm\n");
	} else {
		printf("ACCESS DENIED. That was not it\n");
	}

	return 0;
}