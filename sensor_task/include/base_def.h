#ifndef _BASE_DEF_H_
#define _BASE_DEF_H_
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

union sensor_conf {
    sensor_peripheral_t type;
    struct i2c_bus_config {
        //sensor_peripheral_t _type;
        i2c_port_num_t _port;
        gpio_num_t _sda;
        gpio_num_t _sdc;
    } i2c;
};

typedef union sensor_conf sensor_conf_t;

typedef void (*sensor_handle_t)(sensor_drv_tsk_t task, void *data);

#endif /* _BASE_DEF_H_ */