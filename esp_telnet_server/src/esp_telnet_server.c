//#include <stdio.h>
//#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include "freertos/queue.h"

#include "esp_telnet_server.h"
#include "esp_log.h"
#include <errno.h>

#include "libtelnet.h"
#include "mem_queue.h"

#define TL_TSK_SOCKET_FD    0
#define TL_TSK_CLIENT_FD    1
#define TL_TSK_PIPE_FROM_FD 2
#define TL_TSK_PIPE_TO_FD   3

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
        int from_sock_fd[2];
        int to_sock_fd[2];
    } ln_pipes;
    struct pollfd tlfds[4];
    char *ttype;
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
                        tl_this.ttype = NULL;
                        if(tl_this.handle != NULL) {
                            tl_this.tlfds[TL_TSK_CLIENT_FD].events = POLLIN;
                            tl_this.tl_srv_state = TL_SRV_CLIENT_CONNECTED;
                            ESP_LOGE("tln", "connection from %s", inet_ntoa(cliAddr.sin_addr));
                            static const unsigned char SEND[] = { TELNET_IAC, TELNET_WILL, TELNET_TELOPT_ECHO };
                            send(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, (char *)SEND, sizeof(SEND), 0);
                        } else {
                            write(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, reject_msg, 1+strlen(reject_msg));
                            ESP_LOGE("tln", "handler error");
                            shutdown(tl_this.tlfds[TL_TSK_CLIENT_FD].fd, 2);
                            close(tl_this.tlfds[TL_TSK_CLIENT_FD].fd);
                            tl_this.ttype = NULL;
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
                            tl_this.ttype = NULL;
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
            ESP_LOGE("evth", "Received %.*s", event->data.size, event->data.buffer);
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
                    //tl_this.ttype = (char *)calloc(1,strlen(event->ttype.name)+1);
                    //strcpy(tl_this.ttype, event->ttype.name);
                    //tl_this.ttype = (char *)calloc(1, 1+(event->sub.size));
                    //memcpy(tl_this.ttype, event->sub.buffer, event->sub.size);
                    //ESP_LOGI("evt:rcv", "(%d) %d", event->ttype.cmd, event->ttype._type);
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
            
                default:
                    break;
            }
            break;
        case TELNET_EV_TTYPE:
            ESP_LOGE("evth", "TELNET_EV_TTYPE");
            tl_this.ttype = (char *)calloc(1,strlen(event->ttype.name)+1);
            strcpy(tl_this.ttype, event->ttype.name);
            ESP_LOGI("evt:tty", "(%s) %d", tl_this.ttype, event->ttype.cmd);
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

    tl_this.tl_queue = xQueueCreate(5, sizeof(tl_queue_data_t));
    tl_this.q_cmd = (tl_queue_data_t *)malloc(sizeof(tl_queue_data_t));
    if((!tl_this.tl_queue) || (!tl_this.q_cmd)) {
        ESP_LOGE(tag, "Queue");
        return ESP_FAIL;
    }
    //pipe(tl_this.ln_pipes.from_sock_fd);
    //pipe(tl_this.ln_pipes.to_sock_fd);

    xTaskCreate(vServerTask, "tlnsrv", 4096, NULL, 15, &tl_this.srv_tsk_handle);
    return ESP_OK;
}

QueueHandle_t tl_get_cmd_handle() {
    return tl_this.tl_queue;
}
/*
static void tl_comm_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGE("tlsrv", "Got an event");
}

void tl_server_init(esp_event_loop_handle_t *uevent_handle, esp_event_base_t event_base, int32_t event_id) {
    if(uevent_handle) {
        esp_event_handler_register_with(*uevent_handle, event_base, event_id, tl_comm_handler, NULL);
    }
}
*/