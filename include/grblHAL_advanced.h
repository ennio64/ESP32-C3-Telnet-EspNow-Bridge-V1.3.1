#ifndef GRBLHAL_ADVANCED_H
#define GRBLHAL_ADVANCED_H

#include <stdbool.h>

// Inizializza le funzionalità avanzate
void grblHAL_advanced_init(void);

// Aggiorna lo stato del pin in base alle connessioni attive
void grblHAL_advanced_update_state(bool telnet_connected, bool espnow_connected);

// Imposta il reset on disconnect (usato dalla web interface)
void grblHAL_advanced_set_reset_on_disconnect(bool enable);

// Invia reset al controller SOLO SE abilitato
void grblHAL_advanced_check_and_send_reset(void);

#endif