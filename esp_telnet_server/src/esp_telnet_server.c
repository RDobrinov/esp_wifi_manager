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

static void vServerTask(void *pvParameters) {
    
    const char *tag = "tl:task";
    const telnet_telopt_t cln_opt[] = {
        { TELNET_TELOPT_ECHO,      TELNET_WILL, TELNET_DONT },
        { TELNET_TELOPT_SGA,       TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_LINEMODE,  TELNET_WILL, TELNET_DO   },
        //{ TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO   },
        //{ TELNET_TELOPT_ZMP,       TELNET_WONT, TELNET_DO   },
        //{ TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DO   },
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
                    } else {
                        ESP_LOGW(tag, "Server started");
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
                ESP_LOGW("poll", "result");
                if(tl_this.tlfds[TL_TSK_SOCKET_FD].revents & POLLIN) {
                    if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd == -1 ) {
                        struct tl_udata *udata = (struct tl_udata *)malloc(sizeof(struct tl_udata));
                        tl_this.tlfds[TL_TSK_CLIENT_FD].fd = accept(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, (struct sockaddr *)&cliAddr, &cliAddrLen);
                        tl_this.handle = telnet_init(cln_opt, tl_event_handler, 0, udata);
                        tl_this.tty.ttype = NULL;
                        if(tl_this.handle != NULL) {
                            tl_this.tlfds[TL_TSK_CLIENT_FD].events = POLLIN;
                            tl_this.tl_srv_state = TL_SRV_CLIENT_CONNECTED;
                            ESP_LOGE("tln", "connection from %s", inet_ntoa(cliAddr.sin_addr));
                            static const unsigned char SEND[] = { TELNET_IAC, TELNET_WILL, TELNET_TELOPT_ECHO };
                            send(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, (char *)SEND, sizeof(SEND), 0);
                            /* linenoise */
                            
                            /* end */
                        } else {
                            write(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, reject_msg, 1+strlen(reject_msg));
                            ESP_LOGE("tln", "handler error");
                            shutdown(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, 2);
                            close(tl_this.tlfds[TL_TSK_CLIENT_FD].fd);
                            tl_this.tty.ttype = NULL;
                        }
                        
                    } else {
                        int fd_drop = accept(tl_this.tlfds[TL_TSK_SOCKET_FD].fd, (struct sockaddr *)&cliAddr, &cliAddrLen);
                        write(fd_drop, reject_msg, 1+strlen(reject_msg));
                        ESP_LOGE("tln", "connection ftom %s rejected", inet_ntoa(cliAddr.sin_addr));
                        shutdown(fd_drop,2);
                        close(fd_drop);
                    }
                }
                if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd != -1 ) {
                    if(tl_this.tlfds[TL_TSK_CLIENT_FD].revents & POLLIN) {
                        ssize_t len = recv(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, (char *)recv_data, sizeof(recv_data), 0);
                        if( len == 0 ) {
                            telnet_free(tl_this.handle);
                            ESP_LOGW(tag, "Handler destroyed");
                            shutdown(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, 2);
                            close(tl_this.tlfds[TL_TSK_CLIENT_FD].fd);
                            tl_this.tlfds[TL_TSK_CLIENT_FD].fd = -1;
                            tl_this.tty.ttype = NULL;
                            ESP_LOGW(tag, "Client disconnected");
                        } else {
                            //recv_data[len] = 0x00;
                            //ESP_LOGW(tag, "%s" ,recv_data);
                            for(int i=0; i<len; i++){
                                if(recv_data[i] == TELNET_IAC) {
                                    if(recv_data[i+1] != TELNET_SE ) { 
                                        ESP_LOGI("loop", "%s %s %s", get_cmd(recv_data[i]), get_cmd(recv_data[i+1]), get_opt(recv_data[i+2]));
                                        i += 2;
                                    } else {
                                        ESP_LOGI("loop", "%s %s", get_cmd(recv_data[i]), get_cmd(recv_data[i+1]));
                                        i +=1;
                                    } 
                                } else { printf("%03d ", recv_data[i]); }
                            }
                            printf("\n");
                            telnet_recv(tl_this.handle, (char *)recv_data, len);
                        }
                    }
                }
            }
            if(tl_this.tlfds[TL_TSK_CLIENT_FD].fd != -1) {
                if(!mem_queue_isempty()) {
                    ESP_LOGE("187", "%s", (!mem_queue_isempty()) ? "Data waiting" : "empty");
                    char *buf = (char *)malloc(MEM_QUEUE_SIZE);
                    int count = mem_queue_get(buf, MEM_QUEUE_SIZE-1);
                    ESP_LOGI("loop", "%d bytes received from mem_queue", count);
                    free(buf);
                }
            }
        }
    }    
}
/* Helper */
static const char *get_cmd(unsigned char cmd) {
	static char buffer[4];

	switch (cmd) {
	case 255: return "IAC";
	case 254: return "DONT";
	case 253: return "DO";
	case 252: return "WONT";
	case 251: return "WILL";
	case 250: return "SB";
	case 249: return "GA";
	case 248: return "EL";
	case 247: return "EC";
	case 246: return "AYT";
	case 245: return "AO";
	case 244: return "IP";
	case 243: return "BREAK";
	case 242: return "DM";
	case 241: return "NOP";
	case 240: return "SE";
	case 239: return "EOR";
	case 238: return "ABORT";
	case 237: return "SUSP";
	case 236: return "xEOF";
	default:
		snprintf(buffer, sizeof(buffer), "%d", (int)cmd);
		return buffer;
	}
}

static const char *get_opt(unsigned char opt) {
	switch (opt) {
	case 0: return "BINARY";
	case 1: return "ECHO";
	case 2: return "RCP";
	case 3: return "SGA";
	case 4: return "NAMS";
	case 5: return "STATUS";
	case 6: return "TM";
	case 7: return "RCTE";
	case 8: return "NAOL";
	case 9: return "NAOP";
	case 10: return "NAOCRD";
	case 11: return "NAOHTS";
	case 12: return "NAOHTD";
	case 13: return "NAOFFD";
	case 14: return "NAOVTS";
	case 15: return "NAOVTD";
	case 16: return "NAOLFD";
	case 17: return "XASCII";
	case 18: return "LOGOUT";
	case 19: return "BM";
	case 20: return "DET";
	case 21: return "SUPDUP";
	case 22: return "SUPDUPOUTPUT";
	case 23: return "SNDLOC";
	case 24: return "TTYPE";
	case 25: return "EOR";
	case 26: return "TUID";
	case 27: return "OUTMRK";
	case 28: return "TTYLOC";
	case 29: return "3270REGIME";
	case 30: return "X3PAD";
	case 31: return "NAWS";
	case 32: return "TSPEED";
	case 33: return "LFLOW";
	case 34: return "LINEMODE";
	case 35: return "XDISPLOC";
	case 36: return "ENVIRON";
	case 37: return "AUTHENTICATION";
	case 38: return "ENCRYPT";
	case 39: return "NEW-ENVIRON";
	case 70: return "MSSP";
	case 85: return "COMPRESS";
	case 86: return "COMPRESS2";
	case 93: return "ZMP";
	case 255: return "EXOPL";
	default: return "unknown";
	}
}

static const char *get_slc(unsigned char slc) 
{
    switch(slc) {
        case 1: return "SLC_SYNCH";
        case 2: return "SLC_BRK";
        case 3: return "SLC_IP";
        case 4: return "SLC_AO";
        case 5: return "SLC_AYT";
        case 6: return "SLC_EOR";
        case 7: return "SLC_ABORT";
        case 8: return "SLC_EOF";
        case 9: return "SLC_SUSP";
        case 10: return "SLC_EC";
        case 11: return "SLC_EL";
        case 12: return "SLC_EW";
        case 13: return "SLC_RP";
        case 14: return "SLC_LNEXT";
        case 15: return "SLC_XON";
        case 16: return "SLC_XOFF";
        case 17: return "SLC_FORW1";
        case 18: return "SLC_FORW2";
        case 19: return "SLC_MCL";
        case 20: return "SLC_MCR";
        case 21: return "SLC_MCWL";
        case 22: return "SLC_MCWR";
        case 23: return "SLC_MCBOL";
        case 24: return "SLC_MCEOL";
        case 25: return "SLC_INSRT";
        case 26: return "SLC_OVER";
        case 27: return "SLC_ECR";
        case 28: return "SLC_EWR";
        case 29: return "SLC_EBOL";
        case 30: return "SLC_EEOL";
        default: return "unknown";
    }
}

static const char *get_lm_cmd(unsigned char cmd) {
    switch(cmd) {
        case 1: return "MODE";
        case 2: return "FORWARDMASK";
        case 3: return "SLC";
        case 236: return "EOF";
        case 237: return "SUPS";
        case 238: return "ABORT";
        default: return "unknown";
    }
}
static const char *get_slc_lvl(unsigned char lvl) {
    switch(lvl & TS_SLC_LEVELBITS) {
        case 3: return "SLC_DEFAULT";
        case 2: return "SLC_VALUE";
        case 1: return "SLC_CANTCHANGE";
        case 0: return "SLC_NOSUPPORT";
        default: return "unknown";
    }
}
/* end Helper*/

void tl_event_handler(telnet_t *thisClient, telnet_event_t *event, void *client) {
    //ESP_LOGE("tl:evth", "data to send 0x%02X", event->type);
    switch(event->type) {
        case TELNET_EV_SEND:
            ESP_LOGE("evth", "TELNET_EV_SEND");
            /*for(int i=0; i<event->data.size; i++){
                printf("%03d ", event->data.buffer[i]);
            }*/
            ESP_LOGI("tlev:send", "%s %s %s", get_cmd(event->data.buffer[0]), get_cmd(event->data.buffer[1]), get_opt(event->data.buffer[2]));
            send(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, event->data.buffer, event->data.size, 0);
            break;
        case TELNET_EV_DATA:
            ESP_LOGE("tlev:data", "Received %.*s", event->data.size, event->data.buffer);
            mem_queue_put((char *)event->data.buffer, event->data.size);
            ESP_LOGE("tlev:data", "%s", (!mem_queue_isempty()) ? "Data waiting" : "empty");
            break;
        case TELNET_EV_IAC:
            ESP_LOGE("evth", "TELNET_EV_IAC");
            break;
        case TELNET_EV_WILL:
            ESP_LOGE("evth", "TELNET_EV_WILL");
            ESP_LOGI("tlev:will", "%d (%s)", event->neg.telopt, get_opt(event->neg.telopt));
            switch(event->neg.telopt) {
                case TELNET_TELOPT_TTYPE:
                    telnet_ttype_send(thisClient);
                    break;

                default:
                    break;
            }
            break;
        case TELNET_EV_WONT:
            ESP_LOGE("evth", "TELNET_EV_WONT");
            break;
        case TELNET_EV_DO:
            ESP_LOGE("evth", "TELNET_EV_DO");
            ESP_LOGI("tlev:do", "%d (%s)", event->neg.telopt, get_opt(event->neg.telopt));
            break;
        case TELNET_EV_DONT:
            ESP_LOGE("evth", "TELNET_EV_DONT");
            break;
        case TELNET_EV_SUBNEGOTIATION:
            ESP_LOGE("evth", "TELNET_EV_SUBNEGOTIATION [%s]", get_opt(event->sub.telopt));
            switch (event->sub.telopt) {
                case TELNET_TELOPT_ENVIRON:
                case TELNET_TELOPT_NEW_ENVIRON:
                case TELNET_TELOPT_TTYPE:
                    break;
                case TELNET_TELOPT_ZMP:
                case TELNET_TELOPT_MSSP:
                case TELNET_TELOPT_LINEMODE:
                    ESP_LOGI("sub:lm", "%s", get_lm_cmd(event->data.buffer[0]));
                    for(int i=1; i<event->data.size; i+=3) {
                        ESP_LOGI("sub:lm", "%s %s %d", get_slc(event->data.buffer[i]), get_slc_lvl(event->data.buffer[i+1]), event->data.buffer[i+2]);
                    }
                    ESP_LOGI("evop", "%d", event->data.size);
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
            ESP_LOGE("evth", "TELNET_EV_TTYPE");
            tl_this.tty.ttype = (char *)calloc(1,strlen(event->ttype.name)+1);
            strcpy(tl_this.tty.ttype, event->ttype.name);
            ESP_LOGI("evt:tty", "(%s) %d", tl_this.tty.ttype, event->ttype.cmd);
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
                ESP_LOGI("realoc", "%p:%u", mem_queue, mem_queue->allocated);
                free(mem_queue->holder);
                mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE);
                ESP_LOGI("realoc", "%p:%u", mem_queue, mem_queue->allocated);
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
    ESP_LOGI("memq 62", "len: %d:%d %d", len, mem_queue->tail, mem_queue->allocated - mem_queue->tail);
    if((mem_queue->allocated - mem_queue->tail)<len) {
        ESP_LOGW("memq 64", "alloc %d, tail: %d -> len: %d", mem_queue->allocated, mem_queue->tail, len);
        //size_t new_size = len - mem_queue->tail + 1 + mem_queue->allocated;
        size_t new_size = len + mem_queue->tail + 1; 
        ESP_LOGE("memq 67", "{tail:%d, len:%d new_size:%d}", mem_queue->tail, len, new_size);
        if(new_size <= MEM_QUEUE_MAX_SIZE ) {
            mem_queue->allocated = new_size;
            mem_queue->holder = realloc(mem_queue->holder, mem_queue->allocated);
            if(!mem_queue->holder) {
                ESP_LOGE("memq", "Not enough memory");
                mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE);
                return;
            }
            ESP_LOGW("memq 76", "%d:%d -> %d", len, mem_queue->tail, mem_queue->allocated);
        } else {
            ESP_LOGE("memq", "max_size");
            return;
        }
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
    ESP_LOGI("meminit", "Free heap %lu", esp_get_free_heap_size());
    mem_queue = (mem_queue_storage_t *)malloc(sizeof(mem_queue_storage_t));
    *mem_queue = (mem_queue_storage_t) {NULL, -1, -1, MEM_QUEUE_SIZE};
    ESP_LOGI("memq", "init at %p, %lu", mem_queue, esp_get_free_heap_size());
    return (!(mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE))) ? ESP_FAIL : ESP_OK;
}

/* Linenoise */
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
    int fd = l->ofd;
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

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
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
    int fd = l->ofd, j;
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

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
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
                if (write(l->ofd,&d,1) == -1) return -1;
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

int linenoiseEditStart(struct linenoiseState *l, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l->in_completion = 0;
    l->ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    l->ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
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

    if (write(l->ofd,prompt,l->plen) == -1) return -1;
    return 0;
}

char *linenoiseEditFeed(struct linenoiseState *l) {
    /* Not a TTY, pass control to line reading without character
     * count limits. */
    //if (!isatty(l->ifd)) return linenoiseNoTTY();

    char c;
    int nread;
    char seq[3];

    nread = read(l->ifd,&c,1);
    if (nread <= 0) return NULL;

    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    if ((l->in_completion || c == 9) && completionCallback != NULL) {
        c = completeLine(l,c);
        /* Return on errors */
        /* Out of scope !!! completeLine return last character */
        // if (c < 0) return NULL;
        /* Read next character when 0 */
        if (c == 0) return linenoiseEditMore;
    }

    switch(c) {
    case ENTER:    /* enter */
        history_len--;
        free(history[history_len]);
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
        if (read(l->ifd,seq,1) == -1) break;
        if (read(l->ifd,seq+1,1) == -1) break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                if (read(l->ifd,seq+2,1) == -1) break;
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

/* Other utilities. */
/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
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
