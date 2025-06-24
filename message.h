#ifndef MESSAGE_H
#define MESSAGE_H

typedef struct msg {
    void* content;                          // generico contenuto del messaggio
    struct msg * (*msg_init)(void*);        // creazione msg
    void (*msg_destroy)(struct msg *);      // deallocazione msg
    struct msg * (*msg_copy)(struct msg *); // creazione/copia msg
} msg_t;

// creare ed allocare un nuovo messaggio ospitante
// un generico contenuto content di tipo void*
msg_t* msg_init_string(void* content);

// deallocare un messaggio
void msg_destroy_string(msg_t* msg);

// creare ed allocare un nuovo messaggio ospitante 
// il medesimo contenuto di un messaggio dato, 
// similarmente ai classici costruttori di copia
msg_t* msg_copy_string(msg_t* msg);

#endif // MESSAGE_H