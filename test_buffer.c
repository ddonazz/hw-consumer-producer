#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include "buffer.h"
#include "message.h"

// === Funzioni di Init/Cleanup per la Suite ===
int init_suite_buffer(void)
{
    return 0;
}

int clean_suite_buffer(void)
{
    return 0;
}

// === Strutture dati per i thread helper ===
typedef struct
{
    buffer_t *buffer;
    msg_t *msg_to_put;
    msg_t *msg_put_result; // Per salvare il risultato di put_*
    msg_t *msg_retrieved;  // Per salvare il risultato di get_*
    int num_ops;           // Numero di operazioni da eseguire (per test multi-op)
    int success_count;     // Contatore di operazioni riuscite
} thread_data_t;

// === Funzioni helper per i thread ===
void *producer_thread_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->msg_put_result = put_bloccante(data->buffer, data->msg_to_put);
    return NULL;
}

void *producer_thread_non_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->msg_put_result = put_non_bloccante(data->buffer, data->msg_to_put);
    return NULL;
}

void *consumer_thread_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->msg_retrieved = get_bloccante(data->buffer);
    return NULL;
}

void *consumer_thread_non_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->msg_retrieved = get_non_bloccante(data->buffer);
    return NULL;
}

void *multiple_producer_thread_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->success_count = 0;
    char msg_content[20];
    for (int i = 0; i < data->num_ops; i++)
    {
        sprintf(msg_content, "MSG_P_%d", i);
        msg_t *msg = msg_init_string(msg_content);
        if (put_bloccante(data->buffer, msg) == msg)
        {
            data->success_count++;
        }
        else
        {
            msg_destroy_string(msg); // Non dovrebbe fallire con put bloccante
        }
    }
    return NULL;
}

void *multiple_consumer_thread_blocking(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    data->success_count = 0;
    for (int i = 0; i < data->num_ops; i++)
    {
        msg_t *retrieved_msg = get_bloccante(data->buffer);
        if (retrieved_msg != BUFFER_ERROR)
        {
            data->success_count++;
            msg_destroy_string(retrieved_msg);
        }
    }
    return NULL;
}

// === Test Case ===

// • (P=1; C=0; N=1) Produzione di un solo messaggio in un buffer vuoto
void test_P1_C0_N1_put_single_empty(void)
{
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    msg_t *msg_to_put = msg_init_string("HELLO");
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_to_put);

    msg_t *put_result = put_non_bloccante(buffer, msg_to_put); // O bloccante, uguale per buffer vuoto

    CU_ASSERT_PTR_EQUAL(put_result, msg_to_put);
    CU_ASSERT_EQUAL(buffer->current_size, 1);
    CU_ASSERT_PTR_EQUAL(buffer->messages[0], msg_to_put); // Assume LIFO/FIFO accesso diretto per verifica
    CU_ASSERT_STRING_EQUAL(buffer->messages[0]->content, "HELLO");

    buffer_destroy(buffer); // Dovrebbe distruggere msg_to_put
}

// • (P=0; C=1; N=1) Consumazione di un solo messaggio da un buffer pieno
void test_P0_C1_N1_get_single_full(void)
{
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    msg_t *msg_in_buffer = msg_init_string("WORLD");
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_in_buffer);
    put_non_bloccante(buffer, msg_in_buffer); // Riempi il buffer

    CU_ASSERT_EQUAL(buffer->current_size, 1);

    msg_t *retrieved_msg = get_non_bloccante(buffer); // O bloccante

    CU_ASSERT_PTR_EQUAL(retrieved_msg, msg_in_buffer);
    CU_ASSERT_EQUAL(buffer->current_size, 0);
    CU_ASSERT_STRING_EQUAL(retrieved_msg->content, "WORLD");

    msg_destroy_string(retrieved_msg); // Ora siamo responsabili di questo messaggio
    buffer_destroy(buffer);
}

// • (P=1; C=0; N=1) Produzione in un buffer pieno (non bloccante)
void test_P1_C0_N1_put_non_blocking_full(void)
{
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    msg_t *expected_msg = msg_init_string("EXPECTED_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(expected_msg);
    put_non_bloccante(buffer, expected_msg); // Riempi il buffer

    msg_t *msg_to_fail = msg_init_string("FAIL_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_to_fail);

    msg_t *put_result = put_non_bloccante(buffer, msg_to_fail);

    CU_ASSERT_PTR_EQUAL(put_result, BUFFER_ERROR);
    CU_ASSERT_EQUAL(buffer->current_size, 1);               // Ancora pieno
    CU_ASSERT_PTR_EQUAL(buffer->messages[0], expected_msg); // Contenuto originale
    CU_ASSERT_STRING_EQUAL(buffer->messages[0]->content, "EXPECTED_MSG");

    msg_destroy_string(msg_to_fail); // Dobbiamo distruggerlo noi perché put è fallito
    buffer_destroy(buffer);          // Distrugge expected_msg
}

// • (P=0; C=1; N=1) Consumazione da un buffer vuoto (non bloccante)
void test_P0_C1_N1_get_non_blocking_empty(void)
{
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    msg_t *get_result = get_non_bloccante(buffer);

    CU_ASSERT_PTR_EQUAL(get_result, BUFFER_ERROR);
    CU_ASSERT_EQUAL(buffer->current_size, 0); // Ancora vuoto

    buffer_destroy(buffer);
}

// Consumazione bloccante da un buffer inizialmente vuoto (esempio dalla traccia)
void test_blocking_consumer_initially_empty(void)
{
    pthread_t consumer_tid;
    thread_data_t data;

    data.buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(data.buffer);
    data.msg_retrieved = NULL; // Inizializza per sicurezza

    // Creazione del consumatore che si bloccherà
    pthread_create(&consumer_tid, NULL, consumer_thread_blocking, &data);

    // Diamo un po' di tempo al consumatore per bloccarsi (non ideale per test unitari robusti, ma semplice)
    // In un sistema reale, si userebbero meccanismi di segnalazione più sofisticati
    sleep(1); // Sleep per 1 secondo. Rimuovere o ridurre se causa problemi/lentezza.
              // L'assunto è che 1s sia sufficiente per il thread consumer per chiamare get_bloccante e sospendersi.

    // Il consumatore dovrebbe essere bloccato. Sblocchiamolo.
    msg_t *go_msg = msg_init_string("GO_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(go_msg);
    msg_t *put_res = put_bloccante(data.buffer, go_msg); // O non_bloccante
    CU_ASSERT_PTR_EQUAL(put_res, go_msg);

    pthread_join(consumer_tid, NULL); // Aspetta che il consumatore termini

    CU_ASSERT_PTR_NOT_NULL(data.msg_retrieved);
    if (data.msg_retrieved)
    { // Controlla per evitare dereferenziazione NULL
        CU_ASSERT_STRING_EQUAL(data.msg_retrieved->content, "GO_MSG");
        CU_ASSERT_PTR_EQUAL(data.msg_retrieved, go_msg); // Dovrebbe essere lo stesso puntatore
    }
    CU_ASSERT_EQUAL(data.buffer->current_size, 0); // Il buffer dovrebbe essere di nuovo vuoto

    if (data.msg_retrieved)
    {
        msg_destroy_string(data.msg_retrieved); // Il consumatore "possiede" il messaggio
    }
    buffer_destroy(data.buffer);
}

// • (P=1; C=1; N=1) Consumazione e produzione concorrente di un messaggio da un buffer unitario; prima il consumatore
// Questo è simile al test_blocking_consumer_initially_empty, il consumatore si blocca, poi il produttore sblocca.
void test_P1_C1_N1_concurrent_consumer_first(void)
{
    test_blocking_consumer_initially_empty(); // Riusa la logica
}

// • (P=1; C=1; N=1) Consumazione e produzione concorrente di un messaggio in un buffer unitario; prima il produttore
void test_P1_C1_N1_concurrent_producer_first(void)
{
    pthread_t producer_tid, consumer_tid;
    thread_data_t producer_data, consumer_data;

    buffer_t *common_buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(common_buffer);

    producer_data.buffer = common_buffer;
    producer_data.msg_to_put = msg_init_string("PROD_FIRST");
    CU_ASSERT_PTR_NOT_NULL_FATAL(producer_data.msg_to_put);
    producer_data.msg_put_result = NULL;

    consumer_data.buffer = common_buffer;
    consumer_data.msg_retrieved = NULL;

    // Avvia il produttore, che dovrebbe avere successo immediatamente
    pthread_create(&producer_tid, NULL, producer_thread_blocking, &producer_data);
    pthread_join(producer_tid, NULL); // Assicurati che il produttore finisca prima di avviare il consumatore

    CU_ASSERT_PTR_EQUAL(producer_data.msg_put_result, producer_data.msg_to_put);
    CU_ASSERT_EQUAL(common_buffer->current_size, 1);

    // Avvia il consumatore, che dovrebbe avere successo immediatamente
    pthread_create(&consumer_tid, NULL, consumer_thread_blocking, &consumer_data);
    pthread_join(consumer_tid, NULL);

    CU_ASSERT_PTR_NOT_NULL(consumer_data.msg_retrieved);
    if (consumer_data.msg_retrieved)
    {
        CU_ASSERT_STRING_EQUAL(consumer_data.msg_retrieved->content, "PROD_FIRST");
        CU_ASSERT_PTR_EQUAL(consumer_data.msg_retrieved, producer_data.msg_to_put);
    }
    CU_ASSERT_EQUAL(common_buffer->current_size, 0);

    if (consumer_data.msg_retrieved)
    {
        msg_destroy_string(consumer_data.msg_retrieved);
    }
    // producer_data.msg_to_put è ora "posseduto" e distrutto tramite consumer_data.msg_retrieved
    buffer_destroy(common_buffer);
}

// • (P>1; C=0; N=1) Produzione concorrente di molteplici messaggi in un buffer unitario vuoto
// Due produttori, uno riesce, l'altro si blocca. Poi un get sblocca il secondo.
void test_Pgt1_C0_N1_concurrent_puts_blocking(void)
{
    pthread_t p1_tid, p2_tid;
    thread_data_t p1_data, p2_data;
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    p1_data.buffer = buffer;
    p1_data.msg_to_put = msg_init_string("P1_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(p1_data.msg_to_put);
    p1_data.msg_put_result = NULL;

    p2_data.buffer = buffer;
    p2_data.msg_to_put = msg_init_string("P2_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(p2_data.msg_to_put);
    p2_data.msg_put_result = NULL;

    // Avvia P1, dovrebbe riuscire e riempire il buffer
    pthread_create(&p1_tid, NULL, producer_thread_blocking, &p1_data);
    // Dai un attimo a P1 per eseguire e riempire
    // sleep(0.1); // Molto breve, o usa join se vuoi essere seriale per il primo put

    // Avvia P2, dovrebbe bloccarsi perché il buffer è pieno
    pthread_create(&p2_tid, NULL, producer_thread_blocking, &p2_data);
    sleep(1); // Dai tempo a P2 per provare a mettere e bloccarsi

    // Verifica che P1 sia riuscito (o attendi che finisca)
    pthread_join(p1_tid, NULL); // Assicura che P1 abbia finito
    CU_ASSERT_PTR_EQUAL(p1_data.msg_put_result, p1_data.msg_to_put);
    CU_ASSERT_EQUAL(buffer->current_size, 1);
    CU_ASSERT_PTR_EQUAL(buffer->messages[0], p1_data.msg_to_put);

    // Ora consuma il messaggio di P1, questo dovrebbe sbloccare P2
    msg_t *retrieved_p1_msg = get_bloccante(buffer);
    CU_ASSERT_PTR_EQUAL(retrieved_p1_msg, p1_data.msg_to_put);
    msg_destroy_string(retrieved_p1_msg); // Distruggi il messaggio recuperato

    // Aspetta che P2 finisca (ora dovrebbe essere sbloccato)
    pthread_join(p2_tid, NULL);
    CU_ASSERT_PTR_EQUAL(p2_data.msg_put_result, p2_data.msg_to_put);
    CU_ASSERT_EQUAL(buffer->current_size, 1); // P2 ha messo il suo messaggio
    CU_ASSERT_PTR_EQUAL(buffer->messages[0], p2_data.msg_to_put);

    buffer_destroy(buffer); // Distruggerà p2_data.msg_to_put
}

// • (P=0; C>1; N=1) Consumazione concorrente di molteplici messaggi da un buffer unitario pieno
void test_P0_Cgt1_N1_concurrent_gets_blocking(void)
{
    pthread_t c1_tid, c2_tid;
    thread_data_t c1_data, c2_data;
    buffer_t *buffer = buffer_init(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    msg_t *initial_msg = msg_init_string("INITIAL_MSG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(initial_msg);
    put_bloccante(buffer, initial_msg); // Riempi il buffer

    c1_data.buffer = buffer;
    c1_data.msg_retrieved = NULL;
    c2_data.buffer = buffer;
    c2_data.msg_retrieved = NULL;

    // Avvia C1, dovrebbe riuscire e svuotare il buffer
    pthread_create(&c1_tid, NULL, consumer_thread_blocking, &c1_data);
    // sleep(0.1); // Dai un attimo a C1 per eseguire

    // Avvia C2, dovrebbe bloccarsi perché il buffer è vuoto
    pthread_create(&c2_tid, NULL, consumer_thread_blocking, &c2_data);
    sleep(1); // Dai tempo a C2 per provare a prendere e bloccarsi

    pthread_join(c1_tid, NULL); // Assicura che C1 abbia finito
    CU_ASSERT_PTR_EQUAL(c1_data.msg_retrieved, initial_msg);
    CU_ASSERT_EQUAL(buffer->current_size, 0);

    // Ora metti un messaggio per C2, questo dovrebbe sbloccarlo
    msg_t *msg_for_c2 = msg_init_string("MSG_FOR_C2");
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_for_c2);
    put_bloccante(buffer, msg_for_c2);

    pthread_join(c2_tid, NULL); // Aspetta che C2 finisca
    CU_ASSERT_PTR_EQUAL(c2_data.msg_retrieved, msg_for_c2);
    CU_ASSERT_EQUAL(buffer->current_size, 0);

    msg_destroy_string(c1_data.msg_retrieved);
    msg_destroy_string(c2_data.msg_retrieved);
    buffer_destroy(buffer);
}

// • (P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer vuoto; il buffer non si riempe
void test_Pgt1_C0_Ngt1_concurrent_puts_no_fill(void)
{
    const int NUM_PRODUCERS = 2;
    const int BUFFER_SIZE = 5;
    pthread_t p_tids[NUM_PRODUCERS];
    thread_data_t p_data[NUM_PRODUCERS];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    char msg_content[20];

    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        p_data[i].buffer = buffer;
        sprintf(msg_content, "MSG_NP_%d", i);
        p_data[i].msg_to_put = msg_init_string(msg_content);
        CU_ASSERT_PTR_NOT_NULL_FATAL(p_data[i].msg_to_put);
        p_data[i].msg_put_result = NULL;
        pthread_create(&p_tids[i], NULL, producer_thread_blocking, &p_data[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        pthread_join(p_tids[i], NULL);
        CU_ASSERT_PTR_EQUAL(p_data[i].msg_put_result, p_data[i].msg_to_put);
    }

    CU_ASSERT_EQUAL(buffer->current_size, NUM_PRODUCERS);
    // Verifica opzionale del contenuto (ordine potrebbe non essere garantito)
    // Per semplicità, ci fidiamo che i messaggi siano lì se current_size è corretto
    // e put_result era corretto.

    buffer_destroy(buffer); // Distruggerà i messaggi messi
}

// • (P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer pieno; il buffer è già saturo (con put_non_bloccante)
void test_Pgt1_C0_Ngt1_concurrent_puts_nonblocking_on_full(void)
{
    const int NUM_PRODUCERS_FAIL = 2;
    const int BUFFER_SIZE = 1; // Un buffer piccolo per saturarlo facilmente
    pthread_t p_tids[NUM_PRODUCERS_FAIL];
    thread_data_t p_data[NUM_PRODUCERS_FAIL];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    // Riempi il buffer
    msg_t *initial_fill_msg = msg_init_string("FULL");
    CU_ASSERT_PTR_NOT_NULL_FATAL(initial_fill_msg);
    put_bloccante(buffer, initial_fill_msg);
    CU_ASSERT_EQUAL(buffer->current_size, BUFFER_SIZE);

    char msg_content[20];
    for (int i = 0; i < NUM_PRODUCERS_FAIL; i++)
    {
        p_data[i].buffer = buffer;
        sprintf(msg_content, "MSG_FAIL_%d", i);
        p_data[i].msg_to_put = msg_init_string(msg_content); // Questi verranno distrutti
        CU_ASSERT_PTR_NOT_NULL_FATAL(p_data[i].msg_to_put);
        p_data[i].msg_put_result = (msg_t *)0x1; // Valore non NULL e non BUFFER_ERROR per vedere se cambia
        pthread_create(&p_tids[i], NULL, producer_thread_non_blocking, &p_data[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS_FAIL; i++)
    {
        pthread_join(p_tids[i], NULL);
        CU_ASSERT_PTR_EQUAL(p_data[i].msg_put_result, BUFFER_ERROR);
        msg_destroy_string(p_data[i].msg_to_put); // Distruggiamo perché put_non_bloccante è fallito
    }

    CU_ASSERT_EQUAL(buffer->current_size, BUFFER_SIZE);           // Dimensione non cambiata
    CU_ASSERT_STRING_EQUAL(buffer->messages[0]->content, "FULL"); // Contenuto originale

    buffer_destroy(buffer); // Distrugge initial_fill_msg
}

// • (P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer vuoto; il buffer si satura in corso
void test_Pgt1_C0_Ngt1_concurrent_puts_fill_and_block(void)
{
    const int NUM_PRODUCERS = 3;
    const int BUFFER_SIZE = 2; // Buffer più piccolo del numero di produttori
    pthread_t p_tids[NUM_PRODUCERS];
    thread_data_t p_data[NUM_PRODUCERS];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    char msg_content[20];
    msg_t *msgs_to_put[NUM_PRODUCERS]; // Per tenere traccia dei puntatori originali per le asserzioni

    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        p_data[i].buffer = buffer;
        sprintf(msg_content, "MSG_SAT_%d", i);
        msgs_to_put[i] = msg_init_string(msg_content); // Salva il puntatore originale
        CU_ASSERT_PTR_NOT_NULL_FATAL(msgs_to_put[i]);
        p_data[i].msg_to_put = msgs_to_put[i]; // Passalo al thread_data
        p_data[i].msg_put_result = NULL;
        pthread_create(&p_tids[i], NULL, producer_thread_blocking, &p_data[i]);
    }

    // Dai tempo ai primi BUFFER_SIZE produttori di riempire il buffer
    // e al/ai rimanente/i di bloccarsi. usleep è preferibile a sleep per test più veloci.
    usleep(200000); // 0.2 secondi, un po' più di margine rispetto a 0.1s

    CU_ASSERT_EQUAL(buffer->current_size, BUFFER_SIZE); // Buffer dovrebbe essere pieno (2)

    // Consuma un messaggio per sbloccare uno dei produttori bloccati
    msg_t *retrieved1 = get_bloccante(buffer);
    CU_ASSERT_PTR_NOT_NULL(retrieved1);
    msg_destroy_string(retrieved1);
    // Ora current_size è BUFFER_SIZE - 1. Un produttore bloccato dovrebbe procedere.

    // Aspetta tutti i produttori.
    // Tutti e 3 i produttori dovrebbero completare il loro put_bloccante.
    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        pthread_join(p_tids[i], NULL);
        // Verifica che il risultato di put_bloccante sia il messaggio che si intendeva mettere.
        CU_ASSERT_PTR_EQUAL(p_data[i].msg_put_result, msgs_to_put[i]);
    }

    // Dopo che 3 messaggi sono stati messi e 1 consumato, il buffer dovrebbe essere di nuovo pieno.
    // current_size = (messaggi messi da P0,P1) + (messaggio messo da P2 dopo sblocco) - (retrieved1)
    // Se P0, P1 riempiono -> size 2. P2 si blocca.
    // retrieved1 consuma -> size 1. P2 si sblocca.
    // P2 mette -> size 2.
    CU_ASSERT_EQUAL(buffer->current_size, BUFFER_SIZE); // Dovrebbe essere di nuovo pieno (2)

    // Svuota e verifica i restanti messaggi. Ce ne sono BUFFER_SIZE (2) rimasti.
    msg_t *retrieved2 = get_bloccante(buffer); // Prende uno dei due messaggi
    CU_ASSERT_PTR_NOT_NULL(retrieved2);
    msg_destroy_string(retrieved2);
    // Ora current_size dovrebbe essere BUFFER_SIZE - 1 (cioè 1)

    msg_t *retrieved3 = get_bloccante(buffer); // Prende l'ultimo messaggio rimasto
    CU_ASSERT_PTR_NOT_NULL(retrieved3);
    msg_destroy_string(retrieved3);
    // Ora current_size dovrebbe essere 0

    CU_ASSERT_EQUAL(buffer->current_size, 0); // Verifica che il buffer sia effettivamente vuoto

    buffer_destroy(buffer); // Non ci dovrebbero essere messaggi rimanenti nel buffer
}

// • (P=0; C>1; N>1) Consumazione concorrente di molteplici messaggi da un buffer pieno
void test_P0_Cgt1_Ngt1_concurrent_gets_from_full_and_block(void)
{
    const int NUM_CONSUMERS = 3;
    const int BUFFER_SIZE = 2; // Buffer più piccolo del numero di consumatori
    pthread_t c_tids[NUM_CONSUMERS];
    thread_data_t c_data[NUM_CONSUMERS];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    char msg_content[20];
    msg_t *initial_msgs[BUFFER_SIZE];

    // Riempi il buffer
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        sprintf(msg_content, "MSG_CG_%d", i);
        initial_msgs[i] = msg_init_string(msg_content);
        CU_ASSERT_PTR_NOT_NULL_FATAL(initial_msgs[i]);
        put_bloccante(buffer, initial_msgs[i]);
    }
    CU_ASSERT_EQUAL(buffer->current_size, BUFFER_SIZE);

    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        c_data[i].buffer = buffer;
        c_data[i].msg_retrieved = NULL;
        pthread_create(&c_tids[i], NULL, consumer_thread_blocking, &c_data[i]);
    }

    // Dai tempo ai primi BUFFER_SIZE consumatori di svuotare il buffer
    // e al/ai rimanente/i di bloccarsi.
    sleep(1);
    CU_ASSERT_EQUAL(buffer->current_size, 0);

    // Metti un messaggio per sbloccare uno dei consumatori bloccati
    msg_t *extra_msg = msg_init_string("EXTRA_MSG_CG");
    CU_ASSERT_PTR_NOT_NULL_FATAL(extra_msg);
    put_bloccante(buffer, extra_msg);

    int retrieved_count = 0;
    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        pthread_join(c_tids[i], NULL);
        if (c_data[i].msg_retrieved != BUFFER_ERROR && c_data[i].msg_retrieved != NULL)
        {
            retrieved_count++;
            // Verifichiamo che i messaggi siano quelli attesi (un po' complicato per l'ordine)
            // Per semplicità, distruggiamo e basta
            msg_destroy_string(c_data[i].msg_retrieved);
        }
    }

    CU_ASSERT_EQUAL(retrieved_count, NUM_CONSUMERS);
    CU_ASSERT_EQUAL(buffer->current_size, 0);

    buffer_destroy(buffer);
}

// • (P>1; C>1; N=1) Consumazioni e produzioni concorrenti di molteplici messaggi in un buffer unitario
void test_Pgt1_Cgt1_N1_stress_unitary(void)
{
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int OPS_PER_THREAD = 5;
    const int BUFFER_SIZE = 1;

    pthread_t p_tids[NUM_PRODUCERS];
    pthread_t c_tids[NUM_CONSUMERS];
    thread_data_t p_data[NUM_PRODUCERS];
    thread_data_t c_data[NUM_CONSUMERS];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        p_data[i].buffer = buffer;
        p_data[i].num_ops = OPS_PER_THREAD;
        p_data[i].success_count = 0;
        pthread_create(&p_tids[i], NULL, multiple_producer_thread_blocking, &p_data[i]);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        c_data[i].buffer = buffer;
        c_data[i].num_ops = OPS_PER_THREAD; // Assume #P*OpsP == #C*OpsC
        c_data[i].success_count = 0;
        pthread_create(&c_tids[i], NULL, multiple_consumer_thread_blocking, &c_data[i]);
    }

    int total_produced = 0;
    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        pthread_join(p_tids[i], NULL);
        total_produced += p_data[i].success_count;
    }
    int total_consumed = 0;
    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        pthread_join(c_tids[i], NULL);
        total_consumed += c_data[i].success_count;
    }

    CU_ASSERT_EQUAL(total_produced, NUM_PRODUCERS * OPS_PER_THREAD);
    CU_ASSERT_EQUAL(total_consumed, NUM_CONSUMERS * OPS_PER_THREAD);
    CU_ASSERT_EQUAL(buffer->current_size, 0); // Se #prodotti == #consumati

    buffer_destroy(buffer);
}

// • (P>1; C>1; N>1) Consumazioni e produzioni concorrenti di molteplici messaggi in un buffer
void test_Pgt1_Cgt1_Ngt1_stress_general(void)
{
    const int NUM_PRODUCERS = 3;
    const int NUM_CONSUMERS = 3;
    const int OPS_PER_THREAD = 10;
    const int BUFFER_SIZE = 5; // Buffer di dimensione intermedia

    pthread_t p_tids[NUM_PRODUCERS];
    pthread_t c_tids[NUM_CONSUMERS];
    thread_data_t p_data[NUM_PRODUCERS];
    thread_data_t c_data[NUM_CONSUMERS];
    buffer_t *buffer = buffer_init(BUFFER_SIZE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(buffer);

    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        p_data[i].buffer = buffer;
        p_data[i].num_ops = OPS_PER_THREAD;
        p_data[i].success_count = 0;
        pthread_create(&p_tids[i], NULL, multiple_producer_thread_blocking, &p_data[i]);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        c_data[i].buffer = buffer;
        // Se i numeri di ops per produttore e consumatore sono diversi,
        // il buffer potrebbe non essere vuoto alla fine.
        c_data[i].num_ops = OPS_PER_THREAD;
        c_data[i].success_count = 0;
        pthread_create(&c_tids[i], NULL, multiple_consumer_thread_blocking, &c_data[i]);
    }

    int total_produced = 0;
    for (int i = 0; i < NUM_PRODUCERS; i++)
    {
        pthread_join(p_tids[i], NULL);
        total_produced += p_data[i].success_count;
    }
    int total_consumed = 0;
    for (int i = 0; i < NUM_CONSUMERS; i++)
    {
        pthread_join(c_tids[i], NULL);
        total_consumed += c_data[i].success_count;
    }

    CU_ASSERT_EQUAL(total_produced, NUM_PRODUCERS * OPS_PER_THREAD);
    CU_ASSERT_EQUAL(total_consumed, NUM_CONSUMERS * OPS_PER_THREAD); // Deve essere uguale a total_produced
    CU_ASSERT_EQUAL(buffer->current_size, 0);

    buffer_destroy(buffer);
}

// === Main Function per CUnit ===
int main()
{
    CU_pSuite pSuite = NULL;

    // Inizializza il registro dei test di CUnit
    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();

    // Aggiungi una suite al registro
    pSuite = CU_add_suite("Buffer_Suite", init_suite_buffer, clean_suite_buffer);
    if (NULL == pSuite)
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // Aggiungi i test alla suite
    if (
        (NULL == CU_add_test(pSuite, "(P=1; C=0; N=1) Produzione di un solo messaggio in un buffer vuoto", test_P1_C0_N1_put_single_empty)) ||
        (NULL == CU_add_test(pSuite, "(P=0; C=1; N=1) Consumazione di un solo messaggio da un buffer pieno", test_P0_C1_N1_get_single_full)) ||
        (NULL == CU_add_test(pSuite, "(P=1; C=0; N=1) Produzione non bloccante in un buffer gia' pieno", test_P1_C0_N1_put_non_blocking_full)) ||
        (NULL == CU_add_test(pSuite, "(P=0; C=1; N=1) Consumazione non bloccante da un buffer vuoto", test_P0_C1_N1_get_non_blocking_empty)) ||
        (NULL == CU_add_test(pSuite, "(P=1; C=1; N=1) Consumazione e produzione concorrente di un messaggio da un buffer unitario; prima il consumatore", test_P1_C1_N1_concurrent_consumer_first)) ||
        (NULL == CU_add_test(pSuite, "(P=1; C=1; N=1) Consumazione e produzione concorrente di un messaggio in un buffer unitario; prima il produttore", test_P1_C1_N1_concurrent_producer_first)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C=0; N=1) Produzione concorrente di molteplici messaggi in un buffer unitario vuoto", test_Pgt1_C0_N1_concurrent_puts_blocking)) ||
        (NULL == CU_add_test(pSuite, "(P=0; C>1; N=1) Consumazione concorrente di molteplici messaggi da un buffer unitario pieno", test_P0_Cgt1_N1_concurrent_gets_blocking)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer vuoto; il buffer non si riempe", test_Pgt1_C0_Ngt1_concurrent_puts_no_fill)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer pieno; il buffer e' gia' saturo", test_Pgt1_C0_Ngt1_concurrent_puts_nonblocking_on_full)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C=0; N>1) Produzione concorrente di molteplici messaggi in un buffer vuoto; il buffer si satura in corso", test_Pgt1_C0_Ngt1_concurrent_puts_fill_and_block)) ||
        (NULL == CU_add_test(pSuite, "(P=0; C>1; N>1) Consumazione concorrente di molteplici messaggi da un buffer pieno", test_P0_Cgt1_Ngt1_concurrent_gets_from_full_and_block)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C>1; N=1) Consumazioni e produzioni concorrenti di molteplici messaggi in un buffer unitario", test_Pgt1_Cgt1_N1_stress_unitary)) ||
        (NULL == CU_add_test(pSuite, "(P>1; C>1; N>1) Consumazioni e produzioni concorrenti di molteplici messaggi in un buffer", test_Pgt1_Cgt1_Ngt1_stress_general)))
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // Esegui tutti i test usando l'interfaccia Basic
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    printf("\n");
    CU_basic_show_failures(CU_get_failure_list());
    printf("\n\n");

    // Ottieni il numero di test falliti
    unsigned int num_failures = CU_get_number_of_failures();

    // Pulisci il registro
    CU_cleanup_registry();

    // Restituisce un codice di errore se ci sono stati fallimenti
    return (num_failures > 0) ? 1 : CU_get_error();
}