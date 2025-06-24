#include <pthread.h> 
#include <stdbool.h> 
#include <stdio.h>   
#include <stdlib.h>  
#include "buffer.h"  

// Inizializza un buffer thread-safe
buffer_t* buffer_init(unsigned int max_size){
    buffer_t* buffer = (buffer_t*) malloc(sizeof(buffer_t));
    buffer->messages = (msg_t**) malloc(sizeof(msg_t*) * max_size);
    buffer->max_size = max_size;
    buffer->current_size = 0;

    // Inizializza mutex per accesso esclusivo
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        perror("Mutex initialization failed!");
        exit(EXIT_FAILURE);
    }

    // Inizializza variabili di condizione per segnalazione pieno/vuoto
    if (pthread_cond_init(&buffer->is_not_full, NULL) != 0 || pthread_cond_init(&buffer->is_not_empty, NULL) != 0) {
        perror("Condition variables initialization failed!");
        exit(EXIT_FAILURE);
    }

    return buffer;
}

// Dealloca tutte le risorse del buffer
void buffer_destroy(buffer_t* buffer) {
    // Distrugge i messaggi rimanenti usando il loro distruttore specifico
    while (buffer->current_size > 0) {
        msg_t* msg_to_destroy = buffer->messages[--buffer->current_size];
        if (msg_to_destroy != NULL) {
            msg_to_destroy->msg_destroy(msg_to_destroy); 
        }
    }

    free(buffer->messages);
    pthread_mutex_destroy(&buffer->mutex); // Distrugge il mutex
    pthread_cond_destroy(&buffer->is_not_full); // Distrugge is_not_full
    pthread_cond_destroy(&buffer->is_not_empty); // Distrugge is_not_empty
    free(buffer);
}

// Inserisce un messaggio, bloccante se il buffer è pieno
msg_t* put_bloccante(buffer_t* buffer, msg_t* msg) {
    if (msg != NULL) {
        pthread_mutex_lock(&buffer->mutex); // Blocca l'accesso

        // Attende finché il buffer non è più pieno
        while (buffer->current_size >= buffer->max_size) {
            pthread_cond_wait(&buffer->is_not_full, &buffer->mutex); 
        }

        buffer->messages[buffer->current_size] = msg;
        buffer->current_size++;
        pthread_cond_signal(&buffer->is_not_empty); // Segnala che non è più vuoto
        pthread_mutex_unlock(&buffer->mutex); // Sblocca l'accesso
    }

    return msg;
}

// Inserisce un messaggio, non bloccante (fallisce se il buffer è pieno)
msg_t* put_non_bloccante(buffer_t* buffer, msg_t* msg) {
    if (msg != NULL) {
        pthread_mutex_lock(&buffer->mutex); // Blocca l'accesso

        if (buffer->current_size < buffer->max_size) { // Se c'è spazio
            buffer->messages[buffer->current_size] = msg;
            buffer->current_size++;
            pthread_cond_signal(&buffer->is_not_empty); // Segnala che non è più vuoto
            pthread_mutex_unlock(&buffer->mutex); // Sblocca l'accesso
        } else {
            pthread_mutex_unlock(&buffer->mutex); // Sblocca e ritorna errore
            return BUFFER_ERROR; 
        }
    }

    return msg;
}

// Estrae un messaggio, bloccante se il buffer è vuoto
msg_t* get_bloccante(buffer_t* buffer) {
    pthread_mutex_lock(&buffer->mutex); // Blocca l'accesso

    // Attende finché il buffer non è più vuoto
    while (buffer->current_size <= 0) {
        pthread_cond_wait(&buffer->is_not_empty, &buffer->mutex);
    }
    buffer->current_size--;
    msg_t* msg = buffer->messages[buffer->current_size];
    
    pthread_cond_signal(&buffer->is_not_full); // Segnala che non è più pieno

    pthread_mutex_unlock(&buffer->mutex); // Sblocca l'accesso
    
    return msg;
}

// Estrae un messaggio, non bloccante (fallisce se il buffer è vuoto)
msg_t* get_non_bloccante(buffer_t* buffer) {
    pthread_mutex_lock(&buffer->mutex); // Blocca l'accesso

    if (buffer->current_size <= 0) { // Se è vuoto
        pthread_mutex_unlock(&buffer->mutex); // Sblocca e ritorna errore
        return BUFFER_ERROR;
    }
    buffer->current_size--;
    msg_t* msg = buffer->messages[buffer->current_size];
    
    pthread_cond_signal(&buffer->is_not_full); // Segnala che non è più pieno

    pthread_mutex_unlock(&buffer->mutex); // Sblocca l'accesso
    
    return msg;
}