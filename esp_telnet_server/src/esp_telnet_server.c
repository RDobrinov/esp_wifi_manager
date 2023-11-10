//#include <stdio.h>
//#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include "freertos/queue.h"

#include "esp_telnet_server.h"
#include "esp_log.h"
#include <errno.h>

#include "libtelnet.h"

#define TL_TSK_SOCKET_FD    0
#define TL_TSK_CLIENT_FD    1
#define TL_TSK_PIPE_FROM_FD 2
#define TL_TSK_PIPE_TO_FD   3

/* Linenoise state used by tl_data */
#define LN_MAX_LINE_SIZE    1024

struct linenoiseState {
    int in_completion;  /* The user pressed TAB and we are now in completion
                         * mode, so input is handled by completeLine(). */
    size_t completion_idx; /* Index of next completion to propose. */
    //int ifd;            /* Terminal stdin file descriptor. */
    //int ofd;            /* Terminal stdout file descriptor. */
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

/* Telnet */
typedef struct tl_client {
    int fd;
} tl_client_t;

typedef struct tl_data {
    telnet_t *handle;
    //tl_client_t tl_cln;
    tl_srv_state_t tl_srv_state;
    TaskHandle_t srv_tsk_handle;
    QueueHandle_t tl_queue;
    tl_queue_data_t *q_cmd;
    struct {
        char *ttype;
        int rows;
        int cols;
        struct linenoiseState *ls;
        char *line;
        char *ln_line;
    } tty;
    struct pollfd tlfds[2];
} tl_data_t;

tl_data_t tl_this;

static void vServerTask(void *pvParameters);
void tl_event_handler(telnet_t *thisClient, telnet_event_t *event, void *client);
//static void tl_comm_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

static const char *get_cmd(unsigned char cmd);
static const char *get_opt(unsigned char opt);
static const char *get_slc(unsigned char slc); 
static const char *get_lm_cmd(unsigned char cmd);
static const char *get_slc_lvl(unsigned char lvl);

/* Memory queue */


int mem_queue_get( char *buf, size_t len);
void mem_queue_put( char *buf, size_t len);
bool mem_queue_isempty();
esp_err_t mem_queue_init();

#define MEM_QUEUE_MAX_SIZE  2048
#define MEM_QUEUE_SIZE      128

typedef struct mem_queue_storage {
    char *holder;
    int head;
    int tail;
    size_t allocated;
} mem_queue_storage_t;

static mem_queue_storage_t *mem_queue = NULL;

/* Linenoise component */

//extern char *linenoiseEditMore;
char *linenoiseEditMore = "Static pointer to linenoiseEditMore";

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */

typedef struct linenoiseCompletions {
  size_t len;
  char **cvec;
} linenoiseCompletions;

/* Completion API. */
typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
typedef char*(linenoiseHintsCallback)(const char *, int *color, int *bold);
typedef void(linenoiseFreeHintsCallback)(void *);

/* Non blocking API. */
//int linenoiseEditStart(struct linenoiseState *l, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
int linenoiseEditStart(struct linenoiseState *l, char *buf, size_t buflen, const char *prompt);
char *linenoiseEditFeed(struct linenoiseState *l);
void linenoiseEditStop(struct linenoiseState *l);
void linenoiseHide(struct linenoiseState *l);
void linenoiseShow(struct linenoiseState *l);
void linenoiseFree(void *ptr);

/* Other utilities. */
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
void linenoisePrintKeyCodes(void);
void linenoiseMaskModeEnable(void);
void linenoiseMaskModeDisable(void);

#define REFRESH_CLEAN (1<<0)    // Clean the old prompt from the screen
#define REFRESH_WRITE (1<<1)    // Rewrite the prompt on the screen.
#define REFRESH_ALL (REFRESH_CLEAN|REFRESH_WRITE) // Do both.

#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;
//static char *linenoiseNoTTY(void);
//static void refreshLineWithCompletion(struct linenoiseState *ls, linenoiseCompletions *lc, int flags);
//static void refreshLineWithFlags(struct linenoiseState *l, int flags);

static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
//static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

static void vServerTask(void *pvParameters) {
    
    const char *tag = "tl:task";
    const telnet_telopt_t cln_opt[] = {
        { TELNET_TELOPT_ECHO,      TELNET_WILL, TELNET_DONT },
        { TELNET_TELOPT_SGA,       TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_LINEMODE,  TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_BINARY,    TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_NAWS,      TELNET_WILL, TELNET_DO   },
        { -1, 0, 0 }
    };

    struct tl_udata {
        int sockfd;
    };

    struct sockaddr_in cliAddr;
    socklen_t cliAddrLen = (socklen_t)sizeof(cliAddr);
    uint8_t recv_data[1024];

    char *reject_msg = "\nToo many connections\n\n";

    ESP_LOGI(tag, "vServerTask");

    while(true) {
        BaseType_t pd_qmsg = xQueueReceive(tl_this.tl_queue, (void *)tl_this.q_cmd, (TickType_t) 10 );
        if(pd_qmsg == pdTRUE) {
            switch (tl_this.q_cmd->cmd) {
            case TL_START:
                if(tl_this.tlfds[TL_TSK_SOCKET_FD].fd == -1) {
                    ESP_LOGW(tag, "Server started");
                    tl_this.tlfds[TL_TSK_SOCKET_FD].fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    struct sockaddr_in srv_addr;
                    srv_addr.sin_family = AF_INET;
                    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
                    srv_addr.sin_port = htons(23);
                    int op_result = bind(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
                    if(op_result == -1) {
                        ESP_LOGE(tag, "bind %d (%s)", errno, strerror(errno));
                    } else {
                        tl_this.tlfds[TL_TSK_SOCKET_FD].events = POLLIN;
                        op_result = listen(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, 1);
                        if(op_result == -1) {
                            ESP_LOGE(tag, "listen %d (%s)", errno, strerror(errno));    
                        } else {
                            tl_this.tl_srv_state = TL_SRV_LISTEN; 
                            break;
                        }
                    }
                    if(op_result == -1) {
                        shutdown(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, 2);
                        close(tl_this.tlfds[TL_TSK_SOCKET_FD].fd);
                    }
                }
                break;
            
            default:
                break;
            }
        }

        if(tl_this.tlfds[TL_TSK_SOCKET_FD].fd != -1) {
            int pollResult = poll(tl_this.tlfds, ((tl_this.tlfds[TL_TSK_CLIENT_FD].fd != -1) ? 2 : 1), 10 / portTICK_PERIOD_MS );
            if(pollResult > 0) {
                if(tl_this.tlfds[TL_TSK_SOCKET_FD].revents & POLLIN) {
                    if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd == -1 ) {
                        struct tl_udata *udata = (struct tl_udata *)malloc(sizeof(struct tl_udata));
                        tl_this.tlfds[TL_TSK_CLIENT_FD].fd = accept(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, (struct sockaddr *)&cliAddr, &cliAddrLen);
                        tl_this.handle = telnet_init(cln_opt, tl_event_handler, 0, udata);
                        tl_this.tty.ttype = NULL;
                        if(tl_this.handle != NULL) {
                            tl_this.tlfds[TL_TSK_CLIENT_FD].events = POLLIN;
                            tl_this.tl_srv_state = TL_SRV_CLIENT_CONNECTED;
                            static const unsigned char SEND[] = { TELNET_IAC, TELNET_WILL, TELNET_TELOPT_ECHO };
                            send(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, (char *)SEND, sizeof(SEND), 0);
                            /* linenoise */
                            tl_this.tty.ls = (struct linenoiseState *)malloc(sizeof(struct linenoiseState));
                            tl_this.tty.line = (char *)malloc(sizeof(char) * LN_MAX_LINE_SIZE);
                            linenoiseEditStart(tl_this.tty.ls, tl_this.tty.line, LN_MAX_LINE_SIZE, "esp32# ");
                            /* end */
                        } else {
                            write(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, reject_msg, 1+strlen(reject_msg));
                            ESP_LOGE("tln", "handler error");
                            shutdown(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, 2);
                            close(tl_this.tlfds[TL_TSK_CLIENT_FD].fd);
                        }
                        
                    } else {
                        int fd_drop = accept(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, (struct sockaddr *)&cliAddr, &cliAddrLen);
                        write(fd_drop, reject_msg, 1+strlen(reject_msg));
                        shutdown(fd_drop,2);
                        close(fd_drop);
                    }
                }
                if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd != -1 ) {
                    if(tl_this.tlfds[TL_TSK_CLIENT_FD].revents & POLLIN) {
                        ssize_t len = recv(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, (char *)recv_data, sizeof(recv_data), 0);
                        if( len == 0 ) {
                            telnet_free(tl_this.handle);
                            shutdown(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, 2);
                            close(tl_this.tlfds[TL_TSK_CLIENT_FD].fd);
                            tl_this.tlfds[TL_TSK_CLIENT_FD].fd = -1;
                            free(tl_this.tty.ttype);
                            free(tl_this.tty.ls);
                            free(tl_this.tty.line);
                            tl_this.tty.ttype = NULL;
                            tl_this.tty.ls = NULL;
                            tl_this.tty.line = NULL;
                            tl_this.tl_srv_state = TL_SRV_LISTEN;
                        } else { telnet_recv(tl_this.handle, (char *)recv_data, len); }
                    }
                }
            }
            if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd != -1) {
                if(!mem_queue_isempty()) {
                    //char *buf = (char *)malloc(MEM_QUEUE_SIZE);
                    //int count = mem_queue_get(buf, MEM_QUEUE_SIZE-1);
                    //free(buf);
                    tl_this.tty.ln_line = linenoiseEditFeed(tl_this.tty.ls);
                    if(tl_this.tty.ln_line != linenoiseEditMore) {
                        linenoiseEditStop(tl_this.tty.ls);
                        if(tl_this.tty.ln_line == NULL) {
                            ESP_LOGI("ln", "Empty line");
                        }
                        ESP_LOGW("ln", "%s", tl_this.tty.ln_line);
                        linenoiseFree(tl_this.tty.ln_line); /* In final - just free line buffer */
                        linenoiseEditStart(tl_this.tty.ls, tl_this.tty.line, LN_MAX_LINE_SIZE, "esp32# ");
                    }
                }
            }
        }
    }    
}

void tl_event_handler(telnet_t *thisClient, telnet_event_t *event, void *client) {
    switch(event->type) {
        case TELNET_EV_SEND:
            send(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, event->data.buffer, event->data.size, 0);
            break;
        case TELNET_EV_DATA:
            //ESP_LOGE("tlev:data", "Received %.*s", event->data.size, event->data.buffer);
            mem_queue_put((char *)event->data.buffer, event->data.size);
            break;
        case TELNET_EV_IAC:
            break;
        case TELNET_EV_WILL:
            switch(event->neg.telopt) {
                case TELNET_TELOPT_TTYPE:
                    telnet_ttype_send(thisClient);
                    break;

                default:
                    break;
            }
            break;
        case TELNET_EV_WONT:
            break;
        case TELNET_EV_DO:
            break;
        case TELNET_EV_DONT:
            break;
        case TELNET_EV_SUBNEGOTIATION:
            switch (event->sub.telopt) {
                case TELNET_TELOPT_ENVIRON:
                case TELNET_TELOPT_NEW_ENVIRON:
                case TELNET_TELOPT_TTYPE:
                    break;
                case TELNET_TELOPT_ZMP:
                case TELNET_TELOPT_MSSP:
                case TELNET_TELOPT_LINEMODE:
                    break;

                case TELNET_TELOPT_NAWS:
                    tl_this.tty.rows = event->sub.buffer[3];
                    tl_this.tty.cols = event->sub.buffer[0];
                    break;

                default:
                    break;
            }
            break;
        case TELNET_EV_TTYPE:
            tl_this.tty.ttype = (char *)calloc(1,strlen(event->ttype.name)+1);
            strcpy(tl_this.tty.ttype, event->ttype.name);
            break;

        default:
            break;  
    }

}

esp_err_t tl_server_init() {

    const char *tag = "tls:init";

    tl_this.tlfds[TL_TSK_SOCKET_FD].fd = -1;
    tl_this.tlfds[TL_TSK_CLIENT_FD].fd = -1;
    tl_this.tl_srv_state = TL_SRV_NOACT;
    tl_this.tty.cols = 80;
    tl_this.tty.rows =24;

    tl_this.tty.ls = NULL;
    tl_this.tty.line = NULL;

    tl_this.tl_queue = xQueueCreate(5, sizeof(tl_queue_data_t));
    tl_this.q_cmd = (tl_queue_data_t *)malloc(sizeof(tl_queue_data_t));
    if((!tl_this.tl_queue) || (!tl_this.q_cmd)) {
        ESP_LOGE(tag, "Queue");
        return ESP_FAIL;
    }

    mem_queue_init();
    xTaskCreate(vServerTask, "tlnsrv", 4096, NULL, 15, &tl_this.srv_tsk_handle);
    return ESP_OK;
}

QueueHandle_t tl_get_cmd_handle() {
    return tl_this.tl_queue;
}

/* Memory queue*/
int mem_queue_get( char *buf, size_t len) {
    int bytes = -1;
    if( mem_queue_init() != ESP_OK ) { return -1; }
    if(!mem_queue_isempty()) {
        if(mem_queue->tail >= (mem_queue->head + len - 1) ) {
            memcpy(buf, (mem_queue->holder+mem_queue->head), len);
            mem_queue->head += len;
            bytes = len;
        } else {
            bytes = (mem_queue->tail-mem_queue->head+1);
            memcpy(buf, (mem_queue->holder+mem_queue->head), bytes);
            mem_queue->head += (bytes);
        }
        if(mem_queue->tail < mem_queue->head) { 
            mem_queue->tail = -1;
            mem_queue->head = -1;
            if(mem_queue->allocated > MEM_QUEUE_SIZE) {
                free(mem_queue->holder);
                mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE);
                mem_queue->allocated = MEM_QUEUE_SIZE;
            }
        }
        return bytes;
    }
    return 0;
}

void mem_queue_put( char *buf, size_t len) {
    if( mem_queue_init() != ESP_OK ) return;
    if(mem_queue->tail == -1) { mem_queue->head = 0; }
    if((mem_queue->allocated - mem_queue->tail)<len) {
        size_t new_size = len + mem_queue->tail + 1; 
        if(new_size <= MEM_QUEUE_MAX_SIZE ) {
            mem_queue->allocated = new_size;
            mem_queue->holder = realloc(mem_queue->holder, mem_queue->allocated);
            if(!mem_queue->holder) {
                ESP_LOGE("memq", "Not enough memory");
                mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE);
                return;
            }
        } else { return; }
    }
    mem_queue->tail++;
    memcpy((mem_queue->holder+mem_queue->tail), buf, len);
    mem_queue->tail += len-1;
}

bool mem_queue_isempty() {
    if( mem_queue_init() != ESP_OK ) return true;
    return (mem_queue->tail == -1) ? true : false;
}

esp_err_t mem_queue_init() {
    if(mem_queue) return ESP_OK;
    mem_queue = (mem_queue_storage_t *)malloc(sizeof(mem_queue_storage_t));
    *mem_queue = (mem_queue_storage_t) {NULL, -1, -1, MEM_QUEUE_SIZE};
    return (!(mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE))) ? ESP_FAIL : ESP_OK;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 *
 * Flags is REFRESH_* macros. The function can just remove the old
 * prompt, just write it, or both. */
static void refreshSingleLine(struct linenoiseState *l, int flags) {
    char seq[64];
    size_t plen = strlen(l->prompt);
    //int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    struct abuf ab;

    while((plen+pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen+len > l->cols) {
        len--;
    }

    abInit(&ab);
    /* Cursor to left edge */
    snprintf(seq,sizeof(seq),"\r");
    abAppend(&ab,seq,strlen(seq));

    if (flags & REFRESH_WRITE) {
        /* Write the prompt and the current buffer content */
        abAppend(&ab,l->prompt,strlen(l->prompt));
        if (maskmode == 1) {
            while (len--) abAppend(&ab,"*",1);
        } else {
            abAppend(&ab,buf,len);
        }
        /* Show hits if any. */
        //refreshShowHints(&ab,l,plen);
    }

    /* Erase to right */
    snprintf(seq,sizeof(seq),"\x1b[0K");
    abAppend(&ab,seq,strlen(seq));

    if (flags & REFRESH_WRITE) {
        /* Move cursor to original position. */
        snprintf(seq,sizeof(seq),"\r\x1b[%dC", (int)(pos+plen));
        abAppend(&ab,seq,strlen(seq));
    }

    //if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    telnet_send(tl_this.handle, ab.b, ab.len);
    abFree(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 *
 * Flags is REFRESH_* macros. The function can just remove the old
 * prompt, just write it, or both. */
static void refreshMultiLine(struct linenoiseState *l, int flags) {
    char seq[64];
    int plen = strlen(l->prompt);
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->oldrows;
    //int fd = l->ofd, j;
    int j;
    struct abuf ab;

    l->oldrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);

    if (flags & REFRESH_CLEAN) {
        if (old_rows-rpos > 0) {
            //lndebug("go down %d", old_rows-rpos);
            snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Now for every row clear it, go up. */
        for (j = 0; j < old_rows-1; j++) {
            //lndebug("clear+up");
            snprintf(seq,64,"\r\x1b[0K\x1b[1A");
            abAppend(&ab,seq,strlen(seq));
        }
    }

    if (flags & REFRESH_ALL) {
        /* Clean the top line. */
        //lndebug("clear");
        snprintf(seq,64,"\r\x1b[0K");
        abAppend(&ab,seq,strlen(seq));
    }

    if (flags & REFRESH_WRITE) {
        /* Write the prompt and the current buffer content */
        abAppend(&ab,l->prompt,strlen(l->prompt));
        if (maskmode == 1) {
            unsigned int i;
            for (i = 0; i < l->len; i++) abAppend(&ab,"*",1);
        } else {
            abAppend(&ab,l->buf,l->len);
        }

        /* Show hits if any. */
        //refreshShowHints(&ab,l,plen);

        /* If we are at the very end of the screen with our prompt, we need to
         * emit a newline and move the prompt to the first column. */
        if (l->pos &&
            l->pos == l->len &&
            (l->pos+plen) % l->cols == 0)
        {
            //lndebug("<newline>");
            abAppend(&ab,"\n",1);
            snprintf(seq,64,"\r");
            abAppend(&ab,seq,strlen(seq));
            rows++;
            if (rows > (int)l->oldrows) l->oldrows = rows;
        }

        /* Move cursor to right position. */
        rpos2 = (plen+l->pos+l->cols)/l->cols; /* Current cursor relative row */
        //lndebug("rpos2 %d", rpos2);

        /* Go up till we reach the expected positon. */
        if (rows-rpos2 > 0) {
            //lndebug("go-up %d", rows-rpos2);
            snprintf(seq,64,"\x1b[%dA", rows-rpos2);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Set column. */
        col = (plen+(int)l->pos) % (int)l->cols;
        //lndebug("set col %d", 1+col);
        if (col)
            snprintf(seq,64,"\r\x1b[%dC", col);
        else
            snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
    }

    //lndebug("\n");
    l->oldpos = l->pos;

    //if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    telnet_send(tl_this.handle, ab.b, ab.len);
    abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLineWithFlags(struct linenoiseState *l, int flags) {
    if (mlmode)
        refreshMultiLine(l,flags);
    else
        refreshSingleLine(l,flags);
}

/* Utility function to avoid specifying REFRESH_ALL all the times. */
static void refreshLine(struct linenoiseState *l) {
    refreshLineWithFlags(l,REFRESH_ALL);
}

/* Hide the current line, when using the multiplexing API. */
void linenoiseHide(struct linenoiseState *l) {
    if (mlmode)
        refreshMultiLine(l,REFRESH_CLEAN);
    else
        refreshSingleLine(l,REFRESH_CLEAN);
}

/* Show the current line, when using the multiplexing API. */
void linenoiseShow(struct linenoiseState *l) {
    if (l->in_completion) {
        //refreshLineWithCompletion(l,NULL,REFRESH_WRITE);
    } else {
        refreshLineWithFlags(l,REFRESH_WRITE);
    }
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c) {
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            if ((!mlmode && l->plen+l->len < l->cols && !hintsCallback)) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                char d = (maskmode==1) ? '*' : c;
                //if (write(l->ofd,&d,1) == -1) return -1;
                telnet_send(tl_this.handle, &d, 1);
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        refreshLine(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        refreshLine(l);
    }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit*() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition.
 *
 * If the function returns non-zero, the caller should handle the
 * returned value as a byte read from the standard input, and process
 * it as usually: this basically means that the function may return a byte
 * read from the termianl but not processed. Otherwise, if zero is returned,
 * the input was consumed by the completeLine() function to navigate the
 * possible completions, and the caller should read for the next characters
 * from stdin. */
static int completeLine(struct linenoiseState *ls, int keypressed) {
    linenoiseCompletions lc = { 0, NULL };
    int nwritten;
    char c = keypressed;

    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        //linenoiseBeep();
        ls->in_completion = 0;
    } else {
        switch(c) {
            case 9: /* tab */
                if (ls->in_completion == 0) {
                    ls->in_completion = 1;
                    ls->completion_idx = 0;
                } else {
                    ls->completion_idx = (ls->completion_idx+1) % (lc.len+1);
                    if (ls->completion_idx == lc.len) {} //linenoiseBeep();
                }
                c = 0;
                break;
            case 27: /* escape */
                /* Re-show original buffer */
                if (ls->completion_idx < lc.len) refreshLine(ls);
                ls->in_completion = 0;
                c = 0;
                break;
            default:
                /* Update buffer and return */
                if (ls->completion_idx < lc.len) {
                    nwritten = snprintf(ls->buf,ls->buflen,"%s",
                        lc.cvec[ls->completion_idx]);
                    ls->len = ls->pos = nwritten;
                }
                ls->in_completion = 0;
                break;
        }

        /* Show completion or original buffer */
        if (ls->in_completion && ls->completion_idx < lc.len) {
            //refreshLineWithCompletion(ls,&lc,REFRESH_ALL);
        } else {
            refreshLine(ls);
        }
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

//int linenoiseEditStart(struct linenoiseState *l, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
int linenoiseEditStart(struct linenoiseState *l, char *buf, size_t buflen, const char *prompt) {
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l->in_completion = 0;
    //l->ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    //l->ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
    l->buf = buf;
    l->buflen = buflen;
    l->prompt = prompt;
    l->plen = strlen(prompt);
    l->oldpos = l->pos = 0;
    l->len = 0;
    l->cols = tl_this.tty.cols; //getColumns(stdin_fd, stdout_fd);
    l->oldrows = 0;
    l->history_index = 0;

    /* Buffer starts empty. */
    l->buf[0] = '\0';
    l->buflen--; /* Make sure there is always space for the nulterm */

    /* If stdin is not a tty, stop here with the initialization. We
     * will actually just read a line from standard input in blocking
     * mode later, in linenoiseEditFeed(). */
    //if (!isatty(l->ifd)) return 0;

    /* Enter raw mode. */
    //if (enableRawMode(l->ifd) == -1) return -1;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    //linenoiseHistoryAdd("");

    //if (write(l->ofd,prompt,l->plen) == -1) return -1;
    telnet_send(tl_this.handle, prompt, l->plen);
    return 0;
}

char *linenoiseEditFeed(struct linenoiseState *l) {
    /* Not a TTY, pass control to line reading without character
     * count limits. */
    //if (!isatty(l->ifd)) return linenoiseNoTTY();

    char c;
    //int nread;
    char seq[3];

    //nread = read(l->ifd,&c,1);
    if(mem_queue_get(&c, 1) == 0) return NULL;
    //if (nread <= 0) return NULL;

    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    if ((l->in_completion || c == 9) && completionCallback != NULL) {
        c = completeLine(l,c);
        /* Return on errors */
        /* Out of scope !!! completeLine return last character */
        /* Still not understand what to do */
        // if (c < 0) return NULL;
        /* Read next character when 0 */
        if (c == 0) return linenoiseEditMore;
    }

    switch(c) {
    case ENTER:    /* enter */
        history_len--;
        //free(history[history_len]);
        if (mlmode) linenoiseEditMoveEnd(l);
        if (hintsCallback) {
            /* Force a refresh without hints to leave the previous
             * line as the user typed it after a newline. */
            linenoiseHintsCallback *hc = hintsCallback;
            hintsCallback = NULL;
            refreshLine(l);
            hintsCallback = hc;
        }
        return strdup(l->buf);
    case CTRL_C:     /* ctrl-c */
        errno = EAGAIN;
        return NULL;
    case BACKSPACE:   /* backspace */
    case 8:     /* ctrl-h */
        linenoiseEditBackspace(l);
        break;
    case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                        line is empty, act as end-of-file. */
        if (l->len > 0) {
            linenoiseEditDelete(l);
        } else {
            history_len--;
            free(history[history_len]);
            errno = ENOENT;
            return NULL;
        }
        break;
    case CTRL_T:    /* ctrl-t, swaps current character with previous. */
        if (l->pos > 0 && l->pos < l->len) {
            int aux = l->buf[l->pos-1];
            l->buf[l->pos-1] = l->buf[l->pos];
            l->buf[l->pos] = aux;
            if (l->pos != l->len-1) l->pos++;
            refreshLine(l);
        }
        break;
    case CTRL_B:     /* ctrl-b */
        linenoiseEditMoveLeft(l);
        break;
    case CTRL_F:     /* ctrl-f */
        linenoiseEditMoveRight(l);
        break;
    case CTRL_P:    /* ctrl-p */
        //linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
        break;
    case CTRL_N:    /* ctrl-n */
        //linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
        break;
    case ESC:    /* escape sequence */
        /* Read the next two bytes representing the escape sequence.
         * Use two calls to handle slow terminals returning the two
         * chars at different times. */
        //if (read(l->ifd,seq,1) == -1) break;
        //if (read(l->ifd,seq+1,1) == -1) break;
        if(mem_queue_get(seq,2) != 2) break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                //if (read(l->ifd,seq+2,1) == -1) break;
                if(mem_queue_get(seq+2,1) == 0) break;
                if (seq[2] == '~') {
                    switch(seq[1]) {
                    case '3': /* Delete key. */
                        linenoiseEditDelete(l);
                        break;
                    }
                }
            } else {
                switch(seq[1]) {
                case 'A': /* Up */
                    //linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
                    break;
                case 'B': /* Down */
                    //linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
                    break;
                case 'C': /* Right */
                    linenoiseEditMoveRight(l);
                    break;
                case 'D': /* Left */
                    linenoiseEditMoveLeft(l);
                    break;
                case 'H': /* Home */
                    linenoiseEditMoveHome(l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(l);
                    break;
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch(seq[1]) {
            case 'H': /* Home */
                linenoiseEditMoveHome(l);
                break;
            case 'F': /* End*/
                linenoiseEditMoveEnd(l);
                break;
            }
        }
        break;
    default:
        if (linenoiseEditInsert(l,c)) return NULL;
        break;
    case CTRL_U: /* Ctrl+u, delete the whole line. */
        l->buf[0] = '\0';
        l->pos = l->len = 0;
        refreshLine(l);
        break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
        l->buf[l->pos] = '\0';
        l->len = l->pos;
        refreshLine(l);
        break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
        linenoiseEditMoveHome(l);
        break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
        linenoiseEditMoveEnd(l);
        break;
    case CTRL_L: /* ctrl+l, clear screen */
        linenoiseClearScreen();
        refreshLine(l);
        break;
    case CTRL_W: /* ctrl+w, delete previous word */
        linenoiseEditDeletePrevWord(l);
        break;
    }
    return linenoiseEditMore;
}

void linenoiseEditStop(struct linenoiseState *l) {
    /* Do not need this */
    /*if (!isatty(l->ifd)) return;
    disableRawMode(l->ifd);
    printf("\n"); */
}

void linenoiseFree(void *ptr) {
    if (ptr == linenoiseEditMore) return; // Protect from API misuse.
    free(ptr);
}

/* Other utilities. */
/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    //if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    //}
    telnet_send(tl_this.handle, "\x1b[H\x1b[2J",7);
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

void linenoiseMaskModeEnable(void) {
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void) {
    maskmode = 0;
}
