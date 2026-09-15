#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <lwip/sockets.h>

#include "tcp_server.h"
#include "serial_handler.h"
#include "config.h"
#include "grblHAL_advanced.h"


static const char *TAG = "TCP_SERVER";


/* ============================================================
 * UART QUEUE
 * ============================================================ */

static QueueHandle_t uart_data_queue = NULL;


/* ============================================================
 * TCP CLIENT TABLE
 *
 * -1  = slot libero
 * >=0 = socket client attivo
 * ============================================================ */

static int tcp_clients[MAX_TCP_CLIENTS];


/* ============================================================
 * CLIENT MUTEX
 * ============================================================ */

static SemaphoreHandle_t clients_mutex = NULL;


/* ============================================================
 * INIT CLIENT TABLE
 * ============================================================ */

static void tcp_clients_init(void)
{
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        tcp_clients[i] = -1;
    }
}


/* ============================================================
 * FIND FREE CLIENT SLOT
 * ============================================================ */

static int tcp_find_free_slot(void)
{
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {

        if (tcp_clients[i] < 0) {
            return i;
        }
    }

    return -1;
}


/* ============================================================
 * CLIENT COUNT
 * ============================================================ */

int tcp_server_get_client_count(void)
{
    int count = 0;

    if (!clients_mutex) {
        return 0;
    }

    if (xSemaphoreTake(
            clients_mutex,
            pdMS_TO_TICKS(100)
        ) == pdTRUE) {

        for (int i = 0; i < MAX_TCP_CLIENTS; i++) {

            if (tcp_clients[i] >= 0) {
                count++;
            }
        }

        xSemaphoreGive(clients_mutex);
    }

    return count;
}


/* ============================================================
 * SEND ALL
 *
 * send() può trasmettere meno byte di quelli richiesti.
 * Questa funzione continua fino all'invio completo.
 * ============================================================ */

static bool tcp_send_all(
    int sock,
    const uint8_t *data,
    int length
)
{
    if (sock < 0 || !data || length <= 0) {
        return false;
    }

    int total_sent = 0;

    while (total_sent < length) {

        int sent = send(
            sock,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent <= 0) {
            return false;
        }

        total_sent += sent;
    }

    return true;
}


/* ============================================================
 * REMOVE CLIENT
 *
 * Deve essere chiamata con clients_mutex già acquisito.
 * ============================================================ */

static void tcp_remove_client_locked(int slot)
{
    if (slot < 0 || slot >= MAX_TCP_CLIENTS) {
        return;
    }

    if (tcp_clients[slot] >= 0) {

        int sock = tcp_clients[slot];

        ESP_LOGI(
            TAG,
            "TCP client %d disconnected",
            slot
        );

        shutdown(sock, SHUT_RDWR);
        close(sock);

        tcp_clients[slot] = -1;
    }
}


/* ============================================================
 * BROADCAST UART DATA
 *
 * Invia la risposta UART a TUTTI i client TCP.
 *
 * Il mutex viene mantenuto durante send() per evitare che
 * il TCP task possa chiudere contemporaneamente lo stesso
 * socket.
 * ============================================================ */

void tcp_broadcast_data(
    const uint8_t *data,
    int length
)
{
    if (!data || length <= 0 || !clients_mutex) {
        return;
    }

    if (xSemaphoreTake(
            clients_mutex,
            pdMS_TO_TICKS(100)
        ) != pdTRUE) {

        ESP_LOGW(
            TAG,
            "Cannot acquire client mutex for broadcast"
        );

        return;
    }


    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {

        int sock = tcp_clients[i];

        if (sock < 0) {
            continue;
        }


        if (!tcp_send_all(
                sock,
                data,
                length
            )) {

            ESP_LOGW(
                TAG,
                "TCP client %d send failed, closing socket",
                i
            );

            shutdown(sock, SHUT_RDWR);
            close(sock);

            tcp_clients[i] = -1;
        }
    }


    xSemaphoreGive(clients_mutex);
}


/* ============================================================
 * TCP SERVER TASK
 * ============================================================ */

static void tcp_server_task(void *pvParameters)
{
    (void)pvParameters;


    int listen_sock;

    struct sockaddr_in addr;

    fd_set readfds;

    int max_fd;


    /* ========================================================
     * CREATE LISTEN SOCKET
     * ======================================================== */

    listen_sock = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (listen_sock < 0) {

        ESP_LOGE(
            TAG,
            "socket() failed: errno=%d (%s)",
            errno,
            strerror(errno)
        );

        vTaskDelete(NULL);
        return;
    }


    /* ========================================================
     * REUSE ADDRESS
     * ======================================================== */

    int opt = 1;

    if (setsockopt(
            listen_sock,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) < 0) {

        ESP_LOGW(
            TAG,
            "setsockopt(SO_REUSEADDR) failed: errno=%d (%s)",
            errno,
            strerror(errno)
        );
    }


    /* ========================================================
     * SERVER ADDRESS
     * ======================================================== */

    memset(
        &addr,
        0,
        sizeof(addr)
    );

    addr.sin_family = AF_INET;

    addr.sin_port = htons(
        TELNET_PORT
    );

    addr.sin_addr.s_addr = htonl(
        INADDR_ANY
    );


    /* ========================================================
     * BIND
     * ======================================================== */

    if (bind(
            listen_sock,
            (struct sockaddr *)&addr,
            sizeof(addr)
        ) < 0) {

        ESP_LOGE(
            TAG,
            "bind() failed: errno=%d (%s)",
            errno,
            strerror(errno)
        );

        close(listen_sock);

        vTaskDelete(NULL);
        return;
    }


    /* ========================================================
     * LISTEN
     *
     * Backlog >= maximum configured clients.
     * ======================================================== */

    if (listen(
            listen_sock,
            MAX_TCP_CLIENTS
        ) < 0) {

        ESP_LOGE(
            TAG,
            "listen() failed: errno=%d (%s)",
            errno,
            strerror(errno)
        );

        close(listen_sock);

        vTaskDelete(NULL);
        return;
    }


    ESP_LOGI(
        TAG,
        "Telnet server listening on port %d",
        TELNET_PORT
    );

    ESP_LOGI(
        TAG,
        "Maximum simultaneous TCP clients: %d",
        MAX_TCP_CLIENTS
    );


    /* ========================================================
     * BUFFERS
     * ======================================================== */

    uint8_t buffer[1024];

    uart_data_t *uart_data = NULL;


    /* ========================================================
     * MAIN SERVER LOOP
     * ======================================================== */

    while (1) {


        /* ====================================================
         * BUILD SELECT SET
         * ==================================================== */

        FD_ZERO(&readfds);

        FD_SET(
            listen_sock,
            &readfds
        );

        max_fd = listen_sock;


        if (clients_mutex &&
            xSemaphoreTake(
                clients_mutex,
                pdMS_TO_TICKS(100)
            ) == pdTRUE) {

            for (int i = 0; i < MAX_TCP_CLIENTS; i++) {

                int sock = tcp_clients[i];

                if (sock >= 0) {

                    FD_SET(
                        sock,
                        &readfds
                    );

                    if (sock > max_fd) {
                        max_fd = sock;
                    }
                }
            }

            xSemaphoreGive(
                clients_mutex
            );
        }


        /* ====================================================
         * SELECT TIMEOUT
         *
         * 50 ms permette anche di controllare continuamente
         * la UART queue.
         * ==================================================== */

        struct timeval tv;

        tv.tv_sec = 0;
        tv.tv_usec = 50000;


        int ret = select(
            max_fd + 1,
            &readfds,
            NULL,
            NULL,
            &tv
        );


        if (ret < 0) {

            if (errno == EINTR) {
                continue;
            }

            ESP_LOGE(
                TAG,
                "select() failed: errno=%d (%s)",
                errno,
                strerror(errno)
            );

            vTaskDelay(
                pdMS_TO_TICKS(10)
            );

            continue;
        }


        /* ====================================================
         * NEW TCP CONNECTION
         * ==================================================== */

        if (ret > 0 &&
            FD_ISSET(
                listen_sock,
                &readfds
            )) {


            struct sockaddr_in client_addr;

            socklen_t client_len =
                sizeof(client_addr);


            int new_sock = accept(
                listen_sock,
                (struct sockaddr *)&client_addr,
                &client_len
            );


            /* ------------------------------------------------
             * ACCEPT ERROR
             * ------------------------------------------------ */

            if (new_sock < 0) {

                ESP_LOGE(
                    TAG,
                    "accept() FAILED: errno=%d (%s)",
                    errno,
                    strerror(errno)
                );

            } else {


                /* --------------------------------------------
                 * NEW CLIENT
                 * -------------------------------------------- */

                if (clients_mutex &&
                    xSemaphoreTake(
                        clients_mutex,
                        pdMS_TO_TICKS(100)
                    ) == pdTRUE) {


                    int slot =
                        tcp_find_free_slot();


                    /* ----------------------------------------
                     * FREE SLOT AVAILABLE
                     * ---------------------------------------- */

                    if (slot >= 0) {

                        tcp_clients[slot] =
                            new_sock;


                        char ip_string[INET_ADDRSTRLEN];

                        const char *ip =
                            inet_ntop(
                                AF_INET,
                                &client_addr.sin_addr,
                                ip_string,
                                sizeof(ip_string)
                            );


                        if (!ip) {
                            ip = "?";
                        }


                        ESP_LOGI(
                            TAG,
                            "TCP client %d connected from %s:%u",
                            slot,
                            ip,
                            ntohs(client_addr.sin_port)
                        );


                        ESP_LOGI(
                            TAG,
                            "TCP clients connected: %d/%d",
                            tcp_server_get_client_count(),
                            MAX_TCP_CLIENTS
                        );

                    }


                    /* ----------------------------------------
                     * NO FREE SLOT
                     * ---------------------------------------- */

                    else {

                        ESP_LOGW(
                            TAG,
                            "TCP connection rejected: "
                            "maximum %d clients reached",
                            MAX_TCP_CLIENTS
                        );


                        shutdown(
                            new_sock,
                            SHUT_RDWR
                        );

                        close(
                            new_sock
                        );
                    }


                    xSemaphoreGive(
                        clients_mutex
                    );

                } else {


                    ESP_LOGW(
                        TAG,
                        "Cannot acquire client mutex"
                    );


                    shutdown(
                        new_sock,
                        SHUT_RDWR
                    );

                    close(
                        new_sock
                    );
                }
            }
        }


        /* ====================================================
         * RECEIVE DATA FROM TCP CLIENTS
         * ==================================================== */

        if (ret > 0 &&
            clients_mutex &&
            xSemaphoreTake(
                clients_mutex,
                pdMS_TO_TICKS(100)
            ) == pdTRUE) {


            for (int i = 0;
                 i < MAX_TCP_CLIENTS;
                 i++) {


                int sock =
                    tcp_clients[i];


                if (sock < 0) {
                    continue;
                }


                if (!FD_ISSET(
                        sock,
                        &readfds
                    )) {

                    continue;
                }


                int len = recv(
                    sock,
                    buffer,
                    sizeof(buffer),
                    0
                );


                /* --------------------------------------------
                 * DISCONNECT / ERROR
                 * -------------------------------------------- */

                if (len <= 0) {


                    int recv_errno = errno;


                    ESP_LOGI(
                        TAG,
                        "TCP client %d disconnected "
                        "(recv=%d errno=%d)",
                        i,
                        len,
                        recv_errno
                    );


                    tcp_remove_client_locked(i);


                    /*
                     * Mantiene il comportamento precedente:
                     * controlla il reset grblHAL quando un
                     * client TCP si disconnette.
                     */

                    grblHAL_advanced_check_and_send_reset();

                }


                /* --------------------------------------------
                 * DATA RECEIVED
                 * -------------------------------------------- */

                else {


                    /*
                     * I dati ricevuti da QUALSIASI client TCP
                     * vengono inviati alla queue centrale UART.
                     *
                     * serial_send_data() è void e gestisce
                     * internamente allocazione e queue.
                     */

                    serial_send_data(
                        buffer,
                        len
                    );
                }
            }


            xSemaphoreGive(
                clients_mutex
            );
        }


        /* ====================================================
         * UART → TCP
         *
         * La serial task mette qui le risposte UART.
         * Ogni risposta viene broadcast a tutti i client.
         * ==================================================== */

        if (uart_data_queue) {


            while (xQueueReceive(
                       uart_data_queue,
                       &uart_data,
                       0
                   ) == pdTRUE) {


                if (uart_data &&
                    uart_data->data &&
                    uart_data->length > 0) {


                    /*
                     * Broadcast a TUTTI i client TCP.
                     */

                    tcp_broadcast_data(
                        uart_data->data,
                        uart_data->length
                    );
                }


                /*
                 * La memoria appartiene alla serial handler
                 * e deve essere liberata con la funzione
                 * corretta.
                 */

                if (uart_data) {

                    serial_free_data(
                        uart_data
                    );

                    uart_data = NULL;
                }
            }
        }
    }


    /* ========================================================
     * SHOULD NEVER REACH HERE
     * ======================================================== */

    shutdown(
        listen_sock,
        SHUT_RDWR
    );

    close(
        listen_sock
    );

    vTaskDelete(NULL);
}


/* ============================================================
 * SET UART QUEUE
 * ============================================================ */

void tcp_server_set_queue(
    QueueHandle_t queue
)
{
    uart_data_queue = queue;
}


/* ============================================================
 * START TCP SERVER
 * ============================================================ */

void tcp_server_start(void)
{
    /* --------------------------------------------------------
     * UART QUEUE MUST BE SET FIRST
     * -------------------------------------------------------- */

    if (!uart_data_queue) {

        ESP_LOGE(
            TAG,
            "TCP server NOT started: UART queue not set"
        );

        return;
    }


    /* --------------------------------------------------------
     * INITIALIZE CLIENT TABLE
     * -------------------------------------------------------- */

    tcp_clients_init();


    /* --------------------------------------------------------
     * CREATE CLIENT MUTEX
     * -------------------------------------------------------- */

    clients_mutex =
        xSemaphoreCreateMutex();


    if (!clients_mutex) {

        ESP_LOGE(
            TAG,
            "Cannot create TCP clients mutex"
        );

        return;
    }


    /* --------------------------------------------------------
     * CREATE TCP SERVER TASK
     * -------------------------------------------------------- */

    BaseType_t result =
        xTaskCreate(
            tcp_server_task,
            "tcp_server",
            TCP_SERVER_TASK_SIZE,
            NULL,
            TCP_SERVER_PRIORITY,
            NULL
        );


    if (result != pdPASS) {

        ESP_LOGE(
            TAG,
            "Cannot create TCP server task"
        );


        vSemaphoreDelete(
            clients_mutex
        );

        clients_mutex = NULL;

        return;
    }


    ESP_LOGI(
        TAG,
        "TCP server task started - "
        "port=%d max_clients=%d",
        TELNET_PORT,
        MAX_TCP_CLIENTS
    );
}