/*
 * Ciupitu Andrei-Valentin, 332CC
 */

#ifndef _UTILS_H
#define _UTILS_H

#define DIE(assertion, call_description) \
	do { \
		if (assertion) { \
			fprintf(stderr, "(%s, %s, %d): ", \
				__FILE__, __FUNCTION__, __LINE__); \
			perror(call_description); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

#endif /* _UTILS_H */
