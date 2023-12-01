#include "driver/i2c_master.h"
#include "driver/gpio.h"

typedef enum {
    NONE = 0,
    TEMPERATURE,
    HUMIDITY,
    PRESSURE,
    MAX
} sensor_magnitude_t;

typedef enum {
    INIT,
    PROCESS,
    STATUS,
    GET_DESC,
    GET_MAG,
    GET_COUNT
} sensor_drv_tsk_t;

typedef enum sensor_peripheral {
    I2C,
    SPI,
    UART
} sensor_peripheral_t;

union sensor_conf_t {
    sensor_peripheral_t type;
    struct {
        sensor_peripheral_t _type;
        i2c_port_num_t _port;
        gpio_num_t _sda;
        gpio_num_t _sdc;
    } i2c;
};

typedef void (*sensor_handle_t)(sensor_drv_tsk_t task, void *data);
