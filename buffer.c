#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "buffer.h"

buffer_t* buffer_init(unsigned int maxsize){
    buffer_t* buffer = (buffer_t*) malloc(sizeof(buffer_t));
	buffer->messages = (msg_t**) malloc(sizeof(msg_t*) * maxsize);
	buffer->maxSize = maxsize;
	buffer->index = 0;

	if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
		perror("Mutex initialization failed!");
		exit(EXIT_FAILURE);
	}

	if (pthread_cond_init(&buffer->isNotFull, NULL) != 0 || pthread_cond_init(&buffer->isNotEmpty, NULL) != 0) {
		perror("Condition variables initialization failed!");
		exit(EXIT_FAILURE);
	}

	return buffer;
}

void buffer_destroy(buffer_t* buffer) {
	while (buffer->index > 0) {
		msg_destroy_string(buffer->messages[--buffer->index]);
	}

	free(buffer->messages);
	pthread_mutex_destroy(&buffer->mutex);
        pthread_cond_destroy(&buffer->isNotFull);
	pthread_cond_destroy(&buffer->isNotEmpty);
	free(buffer);
}

msg_t* put_bloccante(buffer_t* buffer, msg_t* msg) {
    if (msg != NULL) {
        pthread_mutex_lock(&buffer->mutex);

        while (buffer->index >= buffer->maxSize) {
            pthread_cond_wait(&buffer->isNotFull, &buffer->mutex);
        }

        buffer->messages[buffer->index] = msg;
        buffer->index++;
        pthread_cond_signal(&buffer->isNotEmpty);
        pthread_mutex_unlock(&buffer->mutex);
    }

    return msg;
}

msg_t* put_non_bloccante(buffer_t* buffer, msg_t* msg) {
    if (msg != NULL) {
        pthread_mutex_lock(&buffer->mutex);

        if (buffer->index < buffer->maxSize) {
            buffer->messages[buffer->index] = msg;
            buffer->index++;
            pthread_cond_signal(&buffer->isNotEmpty);
            pthread_mutex_unlock(&buffer->mutex);

        } else {
            pthread_mutex_unlock(&buffer->mutex);
            return BUFFER_ERROR;
        }
    }

    return msg;
}

msg_t* get_bloccante(buffer_t* buffer) {
    pthread_mutex_lock(&buffer->mutex);

	while (buffer->index <= 0) {
		pthread_cond_wait(&buffer->isNotEmpty, &buffer->mutex);
	}
	buffer->index--;
	msg_t* msg = buffer->messages[buffer->index];
	
	pthread_cond_signal(&buffer->isNotFull);

	pthread_mutex_unlock(&buffer->mutex); 
	
	return msg;
}

msg_t* get_non_bloccante(buffer_t* buffer) {
   pthread_mutex_lock(&buffer->mutex);

	if (buffer->index <= 0) {
		pthread_mutex_unlock(&buffer->mutex);
		return BUFFER_ERROR;
	}
	buffer->index--;
	msg_t* msg = buffer->messages[buffer->index];
	
	pthread_cond_signal(&buffer->isNotFull);

	pthread_mutex_unlock(&buffer->mutex); 
	
	return msg;
}
