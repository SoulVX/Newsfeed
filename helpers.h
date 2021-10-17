#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <stdio.h>
#include <stdlib.h>

/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)

#define TOPIC_LEN 50
#define CONTINUT_LEN 1500
#define MAX_BUFLEN 100

#define UDP_TYPE_INT 0
#define UDP_TYPE_SHORT 1
#define UDP_TYPE_FLOAT 2
#define UDP_TYPE_STRING 3
#define UDP_TYPE_KICK 4

typedef struct {
	int content_len;
	sockaddr_in from;
	char topic[TOPIC_LEN];
	uint8_t tip_date;      
	char continut[CONTINUT_LEN];
} UDP_message;


enum TCP_TYPE {EXIT, SUBSCRIBE, UNSUBSCRIBE, JOIN};
typedef struct {
	TCP_TYPE tip;
	char topic[TOPIC_LEN];
	bool sf;
} TCP_message;

#endif
