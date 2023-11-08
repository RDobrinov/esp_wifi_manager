#ifndef _ESP_TELNET_SERVER_H_
#define _ESP_TELNET_SERVER_H_

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

//#include "esp_netif.h"
//#include "lwip/ip4_addr.h"
//#include "esp_netif_types.h"
//#include "../lwip/esp_netif_lwip_internal.h"

/* WiFi */
//#include "esp_wifi.h"
//#include "nvs_flash.h"
#include "esp_event.h"
//#include "freertos/event_groups.h"

#define TS_LM_MODE          1
#define TS_LM_FORWARDMASK   2
#define TS_LM_SLC           3
#define TS_LM_EOF           236
#define TS_LM_SUPS          237
#define TS_LM_ABORT         238

#define TS_SLC_SYNCH        1
#define TS_SLC_BRK          2
#define TS_SLC_IP           3
#define TS_SLC_AO           4
#define TS_SLC_AYT          5
#define TS_SLC_EOR          6
#define TS_SLC_ABORT        7
#define TS_SLC_EOF          8
#define TS_SLC_SUSP         9
#define TS_SLC_EC          10
#define TS_SLC_EL          11
#define TS_SLC_EW          12
#define TS_SLC_RP          13
#define TS_SLC_LNEXT       14
#define TS_SLC_XON         15
#define TS_SLC_XOFF        16
#define TS_SLC_FORW1       17
#define TS_SLC_FORW2       18
#define TS_SLC_MCL         19
#define TS_SLC_MCR         20
#define TS_SLC_MCWL        21
#define TS_SLC_MCWR        22
#define TS_SLC_MCBOL       23
#define TS_SLC_MCEOL       24
#define TS_SLC_INSRT       25
#define TS_SLC_OVER        26
#define TS_SLC_ECR         27
#define TS_SLC_EWR         28
#define TS_SLC_EBOL        29
#define TS_SLC_EEOL        30

#define TS_SLC_DEFAULT      3
#define TS_SLC_VALUE        2
#define TS_SLC_CANTCHANGE   1
#define TS_SLC_NOSUPPORT    0
#define TS_SLC_LEVELBITS    3

#define TS_SLC_ACK        128
#define TS_SLC_FLUSHIN     64
#define TS_SLC_FLUSHOUT    32

typedef enum {
    TL_START,
    TL_STOP,
    TL_SHUTDOWN
} tl_cmds_e;

typedef enum {
    TL_SRV_NOACT,
    TL_SRV_LISTEN,
    TL_SRV_CLIENT_CONNECTED
} tl_srv_state_t;

typedef struct tl_queue_data {
    tl_cmds_e cmd;
} tl_queue_data_t;

//void tl_server_init(esp_event_loop_handle_t *uevent_handle, esp_event_base_t event_base, int32_t event_id);
//void tl_();
esp_err_t tl_server_init();
QueueHandle_t tl_get_cmd_handle();

/* Linenoise component */

extern char *linenoiseEditMore;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int in_completion;  /* The user pressed TAB and we are now in completion
                         * mode, so input is handled by completeLine(). */
    size_t completion_idx; /* Index of next completion to propose. */
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t oldrows;     /* Rows used by last refrehsed line (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
};

typedef struct linenoiseCompletions {
  size_t len;
  char **cvec;
} linenoiseCompletions;

/* Non blocking API. */
int linenoiseEditStart(struct linenoiseState *l, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
char *linenoiseEditFeed(struct linenoiseState *l);
void linenoiseEditStop(struct linenoiseState *l);
void linenoiseHide(struct linenoiseState *l);
void linenoiseShow(struct linenoiseState *l);

/* Other utilities. */
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
void linenoisePrintKeyCodes(void);
void linenoiseMaskModeEnable(void);
void linenoiseMaskModeDisable(void);

#endif /* _ESP_TELNET_SERVER_H_ */