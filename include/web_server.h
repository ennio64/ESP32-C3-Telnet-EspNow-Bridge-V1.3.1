#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>

// Avvia server web su porta 80
void web_server_start(void);

// Ferma server web
void web_server_stop(void);

// Ottiene lo stato del server (attivo/inattivo)
bool web_server_is_running(void);

#endif