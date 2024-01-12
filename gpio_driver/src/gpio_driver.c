#include <stdio.h>
#include "gpio_driver.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_chip_info.h"
#include "esp_log.h"

#ifdef CONFIG_GPIO_TEXT_FUNCTIONS
static const char *pure_gpio = "GPIO";
static const char *error_gpio = "GPIOERR";

#ifdef CONFIG_IDF_TARGET_ESP32C6
static const char *cc_iomux_func[][SOC_GPIO_PIN_COUNT] = {
    {"GPIO0", "GPIO1", "GPIO2", "GPIO3", "MTMS", "MTDI", "MTCK", "MTDO", "GPIO8", "GPIO9", "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "GPIO15", "U0TXD", "U0RXD", "SDIO_CMD", "SDIO_CLK", "SDIO_DATA0", "SDIO_DATA1", "SDIO_DATA2", "SDIO_DATA3", "SPICS0", "SPIQ", "SPIWP", "GPIO27", "SPIHD", "SPICLK", "SPID"},
    {"", "", "FSPIQ", "", "FSPIHD", "FSPIWP", "FSPICLK", "FSPID", "", "", "", "", "", "", "", "", "FSPICS0", "FSPICS1", "FSPICS2", "FSPICS3", "FSPICS4", "FSPICS5", "", "", "", "", "", "", "", "", ""}
};

static const char *cc_gpio_matrix_sig[][SIG_GPIO_OUT_IDX] = {
    {"ext_adc_start", "", "", "", "", "", "U0RXD", "U0CTS", "U0DSR", "U1RXD", "U1CTS", "U1DSR", "I2S_MCLK", "I2SO_BCK", "I2SO_WS", "I2SI_SD", "I2SI_BCK", "I2SI_WS", "", "usb_jtag_tdo_bridge", "", "", "", "", "", "", "", "", "cpu_gpio0", "cpu_gpio1", "cpu_gpio2", "cpu_gpio3", "cpu_gpio4", "cpu_gpio5", "cpu_gpio6", "cpu_gpio7", "", "", "", "", "", "", "", "", "", "I2CEXT0_SCL", "I2CEXT0_SDA", "parl_rx_data0", "parl_rx_data1", "parl_rx_data2", "parl_rx_data3", "parl_rx_data4", "parl_rx_data5", "parl_rx_data6", "parl_rx_data7", "parl_rx_data8", "parl_rx_data9", "parl_rx_data10", "parl_rx_data11", "parl_rx_data12", "parl_rx_data13", "parl_rx_data14", "parl_rx_data15", "FSPICLK", "FSPIQ", "FSPID", "FSPIHD", "FSPIWP", "FSPICS0", "parl_rx_clk", "parl_tx_clk", "rmt_sig0", "rmt_sig1", "twai0_rx", "", "", "", "twai1_rx", "", "", "", "", "", "", "", "", "", "pwm0_sync0", "pwm0_sync1", "pwm0_sync2", "pwm0_f0", "pwm0_f1", "pwm0_f2", "pwm0_cap0", "pwm0_cap1", "pwm0_cap2", "", "sig_func_97", "sig_func_98", "sig_func_99", "sig_func_100", "pcnt_sig_ch00", "pcnt_sig_ch10", "pcnt_ctrl_ch00", "pcnt_ctrl_ch10", "pcnt_sig_ch01", "pcnt_sig_ch11", "pcnt_ctrl_ch01", "pcnt_ctrl_ch11", "pcnt_sig_ch02", "pcnt_sig_ch12", "pcnt_ctrl_ch02", "pcnt_ctrl_ch12", "pcnt_sig_ch03", "pcnt_sig_ch13", "pcnt_ctrl_ch03", "pcnt_ctrl_ch13", "", "", "", "", "SPIQ", "SPID", "SPIHD", "SPIWP", "", "", ""},
    {"ledc_ls_sig0", "ledc_ls_sig1", "ledc_ls_sig2", "ledc_ls_sig3", "ledc_ls_sig4", "ledc_ls_sig5", "U0TXD", "U0RTS", "U0DTR", "U1TXD", "U1RTS", "U1DTR", "I2S_MCLK", "I2SO_BCK", "I2SO_WS", "I2SO_SD", "I2SI_BCK", "I2SI_WS", "I2SO_SD1", "usb_jtag_trst", "", "", "", "", "", "", "", "", "cpu_gpio0", "cpu_gpio1", "cpu_gpio2", "cpu_gpio3", "cpu_gpio4", "cpu_gpio5", "cpu_gpio6", "cpu_gpio7", "", "", "", "", "", "", "", "", "", "I2CEXT0_SCL", "I2CEXT0_SDA", "parl_tx_data0", "parl_tx_data1", "parl_tx_data2", "parl_tx_data3", "parl_tx_data4", "parl_tx_data5", "parl_tx_data6", "parl_tx_data7", "parl_tx_data8", "parl_tx_data9", "parl_tx_data10", "parl_tx_data11", "parl_tx_data12", "parl_tx_data13", "parl_tx_data14", "parl_tx_data15", "FSPICLK_mux", "FSPIQ", "FSPID", "FSPIHD", "FSPIWP", "FSPICS0", "sdio_tohostt", "parl_tx_clk", "rmt_sig0", "rmt_sig1", "twai0_tx", "twai0_bus_off_on", "twai0_clkout", "twai0_standby", "twai1_tx", "twai1_bus_off_on", "twai1_clkout", "twai1_standby", "", "", "gpio_sd0", "gpio_sd1", "gpio_sd2", "gpio_sd3", "pwm00a", "pwm00b", "pwm01a", "pwm01b", "pwm02a", "pwm02b", "", "", "", "", "sig_func97", "sig_func98", "sig_func99", "sig_func100", "FSPICS1", "FSPICS2", "FSPICS3", "FSPICS4", "FSPICS5", "", "", "", "", "", "", "", "", "SPICLK_mux", "SPICS0", "SPICS1", "", "", "", "", "SPIQ", "SPID", "SPIHD", "SPIWP", "CLK1", "CLK2", "CLK3"}
};

static const char *cc_iomux_name = "LP";
#endif /* CONFIG_IDF_TARGET_ESP32C6 */

#ifdef CONFIG_IDF_TARGET_ESP32
static const char *cc_iomux_func[][SOC_GPIO_PIN_COUNT] = {
    {"GPIO0", "U0TXD", "GPIO2", "U0RXD", "GPIO4", "GPIO5", "SD_CLK", "SD_DATA0", "SD_DATA1", "SD_DATA2", "SD_DATA3", "SD_CMD", "MTDI", "MTCK", "MTMS", "MTDO", "GPIO16", "GPIO17", "GPIO18", "GPIO19", "", "GPIO21", "GPIO22", "GPIO23", "", "GPIO25", "GPIO26", "GPIO27", "", "", "", "", "GPIO32", "GPIO33", "GPIO34", "GPIO35", "GPIO36", "GPIO37", "GPIO38", "GPIO39"},
    {"CLK_OUT1", "CLK_OUT3", "HSPIWP", "CLK_OUT2", "HSPIHD", "VSPICS0", "SPICLK", "SPIQ", "SPID", "SPIHD", "SPIWP", "SPICS0", "HSPIQ", "HSPID", "HSPICLK", "HSPICS0", "", "", "VSPICLK", "VSPIQ", "", "VSPIHD", "VSPIWP", "VSPID", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""},
    //{"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "GPIO15", "GPIO16", "GPIO17", "GPIO18", "GPIO19", "", "GPIO21", "GPIO22", "GPIO23", "", "GPIO25", "GPIO26", "GPIO27", "", "", "", "", "GPIO32", "GPIO33", "GPIO34", "GPIO35", "GPIO36", "GPIO37", "GPIO38", "GPIO39"},
    {"", "", "HS2_DATA0", "", "HS2_DATA1", "HS1_DATA6", "HS1_CLK", "HS1_DATA0", "HS1_DATA1", "HS1_DATA2", "HS1_DATA3", "HS1_CMD", "HS2_DATA2", "HS2_DATA3", "HS2_CLK", "HS2_CMD", "HS1_DATA4", "HS1_DATA5", "HS1_DATA7", "U0CTS", "", "", "U0RTS", "HS1_STROBE", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""},
    {"", "", "SD_DATA0", "", "SD_DATA1", "", "U1CTS", "U2RTS", "U2CTS", "U1RXD", "U1TXD", "U1RTS", "SD_DATA2", "SD_DATA3", "SD_CLK", "SD_CMD", "U2RXD", "U2TXD", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""},
    {"EMAC_TX_CLK", "EMAC_RXD2", "", "", "EMAC_TX_ER", "EMAC_RX_CLK", "", "", "", "", "", "", "EMAC_TXD3", "EMAC_RX_ER", "EMAC_TXD2", "EMAC_RXD3", "EMAC_CLK_OUT", "EMAC_CLK_180", "", "EMAC_TXD0", "", "EMAC_TX_EN", "EMAC_TXD1", "", "", "EMAC_RXD0", "EMAC_RXD1", "EMAC_RX_DV", "", "", "", "", "", "", "", "", "", "", "", ""}
};

static const char *cc_gpio_matrix_sig[][SIG_GPIO_OUT_IDX] = {
    {"SPICLK","SPIQ","SPID","SPIHD","SPIWP","SPICS0","SPICS1","SPICS2","HSPICLK","HSPIQ","HSPID","HSPICS0","HSPIHD","HSPIWP","U0RXD","U0CTS","U0DSR","U1RXD","U1CTS","","","","","I2S0O_BCK","I2S1O_BCK","I2S0O_WS","I2S1O_WS","I2S0I_BCK","I2S0I_WS","I2CEXT0_SCL","I2CEXT0_SDA","pwm0_sync0","pwm0_sync1","pwm0_sync2","pwm0_f0","pwm0_f1","pwm0_f2","","","pcnt_sig_ch00","pcnt_sig_ch10","pcnt_ctrl_ch00","pcnt_ctrl_ch10","pcnt_sig_ch01","pcnt_sig_ch11","pcnt_ctrl_ch01","pcnt_ctrl_ch11","pcnt_sig_ch02","pcnt_sig_ch12","pcnt_ctrl_ch02","pcnt_ctrl_ch12","pcnt_sig_ch03","pcnt_sig_ch13","pcnt_ctrl_ch03","pcnt_ctrl_ch13","pcnt_sig_ch04","pcnt_sig_ch14","pcnt_ctrl_ch04","pcnt_ctrl_ch14","","","HSPICS1","HSPICS2","VSPICLK","VSPIQ","VSPID","VSPIHD","VSPIWP","VSPICS0","VSPICS1","VSPICS2","pcnt_sig_ch05","pcnt_sig_ch15","pcnt_ctrl_ch05","pcnt_ctrl_ch15","pcnt_sig_ch06","pcnt_sig_ch16","pcnt_ctrl_ch06","pcnt_ctrl_ch16","pcnt_sig_ch07","pcnt_sig_ch17","pcnt_ctrl_ch07","pcnt_ctrl_ch17","rmt_sig0","rmt_sig1","rmt_sig2","rmt_sig3","rmt_sig4","rmt_sig5","rmt_sig6","rmt_sig7","","","","twai_rx","I2CEXT1_SCL","I2CEXT1_SDA","host_card_detect_n_1","host_card_detect_n_2","host_card_write_prt_1","host_card_write_prt_2","host_cardt_n_1","host_cardt_n_2","pwm1_sync0","pwm1_sync1","pwm1_sync2","pwm1_f0","pwm1_f1","pwm1_f2","pwm0_cap0","pwm0_cap1","pwm0_cap2","pwm1_cap0","pwm1_cap1","pwm1_cap2","","","","","","","","","","","","","","","","","","","","","","","","","","I2S0I_DATA0","I2S0I_DATA1","I2S0I_DATA2","I2S0I_DATA3","I2S0I_DATA4","I2S0I_DATA5","I2S0I_DATA6","I2S0I_DATA7","I2S0I_DATA8","I2S0I_DATA9","I2S0I_DATA10","I2S0I_DATA11","I2S0I_DATA12","I2S0I_DATA13","I2S0I_DATA14","I2S0I_DATA15","","","","","","","","","I2S1I_BCK","I2S1I_WS","I2S1I_DATA0","I2S1I_DATA1","I2S1I_DATA2","I2S1I_DATA3","I2S1I_DATA4","I2S1I_DATA5","I2S1I_DATA6","I2S1I_DATA7","I2S1I_DATA8","I2S1I_DATA9","I2S1I_DATA10","I2S1I_DATA11","I2S1I_DATA12","I2S1I_DATA13","I2S1I_DATA14","I2S1I_DATA15","","","","","","","","","I2S0I_H_SYNC","I2S0I_V_SYNC","I2S0I_H_ENABLE","I2S1I_H_SYNC","I2S1I_V_SYNC","I2S1I_H_ENABLE","","","U2RXD","U2CTS","emac_mdc_i","emac_mdi_i","emac_crs_i","emac_col_i","pcmfsync","pcmclk","pcmdin","","","","","","","","","","","","","","","","","","","","","",""},
    {"SPICLK","SPIQ","SPID","SPIHD","SPIWP","SPICS0","SPICS1","SPICS2","HSPICLK","HSPIQ","HSPID","HSPICS0","HSPIHD","HSPIWP","U0TXD","U0RTS","U0DTR","U1TXD","U1RTS","","","","","I2S0O_BCK","I2S1O_BCK","I2S0O_WS","I2S1O_WS","I2S0I_BCK","I2S0I_WS","I2CEXT0_SCL","I2CEXT0_SDA","sdio_tohost_int","pwm00a","pwm00b","pwm01a","pwm01b","pwm02a","pwm02b","","","","","","","","","","","","","","","","","","","","","","","","HSPICS1","HSPICS2","VSPICLK_mux","VSPIQ","VSPID","VSPIHD","VSPIWP","VSPICS0","VSPICS1","VSPICS2","ledc_hs_sig0","ledc_hs_sig1","ledc_hs_sig2","ledc_hs_sig3","ledc_hs_sig4","ledc_hs_sig5","ledc_hs_sig6","ledc_hs_sig7","ledc_ls_sig0","ledc_ls_sig1","ledc_ls_sig2","ledc_ls_sig3","ledc_ls_sig4","ledc_ls_sig5","ledc_ls_sig6","ledc_ls_sig7","rmt_sig0","rmt_sig1","rmt_sig2","rmt_sig3","rmt_sig4","rmt_sig5","rmt_sig6","rmt_sig7","I2CEXT1_SCL","I2CEXT1_SDA","host_ccmd_od_pullup_en_n","host_rst_n_1","host_rst_n_2","gpio_sd0","gpio_sd1","gpio_sd2","gpio_sd3","gpio_sd4","gpio_sd5","gpio_sd6","gpio_sd7","pwm10a","pwm10b","pwm11a","pwm11b","pwm12a","pwm12b","","","","","","","","","","twai_tx","twai_bus_off_on","twai_clkout","","","","","","","","","","","","","","","I2S0O_DATA0","I2S0O_DATA1","I2S0O_DATA2","I2S0O_DATA3","I2S0O_DATA4","I2S0O_DATA5","I2S0O_DATA6","I2S0O_DATA7","I2S0O_DATA8","I2S0O_DATA9","I2S0O_DATA10","I2S0O_DATA11","I2S0O_DATA12","I2S0O_DATA13","I2S0O_DATA14","I2S0O_DATA15","I2S0O_DATA16","I2S0O_DATA17","I2S0O_DATA18","I2S0O_DATA19","I2S0O_DATA20","I2S0O_DATA21","I2S0O_DATA22","I2S0O_DATA23","I2S1I_BCK","I2S1I_WS","I2S1O_DATA0","I2S1O_DATA1","I2S1O_DATA2","I2S1O_DATA3","I2S1O_DATA4","I2S1O_DATA5","I2S1O_DATA6","I2S1O_DATA7","I2S1O_DATA8","I2S1O_DATA9","I2S1O_DATA10","I2S1O_DATA11","I2S1O_DATA12","I2S1O_DATA13","I2S1O_DATA14","I2S1O_DATA15","I2S1O_DATA16","I2S1O_DATA17","I2S1O_DATA18","I2S1O_DATA19","I2S1O_DATA20","I2S1O_DATA21","I2S1O_DATA22","I2S1O_DATA23","","","","","","","","","U2TXD","U2RTS","emac_mdc_o","emac_mdo_o","emac_crs_o","emac_col_o","bt_audio0_irq","bt_audio1_irq","bt_audio2_irq","ble_audio0_irq","ble_audio1_irq","ble_audio2_irq","pcmfsync","pcmclk","pcmdout","ble_audio_sync0_p","ble_audio_sync1_p","ble_audio_sync2_p","","","","","","","","","sig_func224","sig_func225","sig_func226","sig_func227","sig_func228"}    
};
static const char *cc_iomux_name = "RTC";
#endif /* CONFIG_IDF_TARGET_ESP32 */

#endif /* CONFIG_GPIO_TEXT_FUNCTIONS */

/**
 * @brief Type of GPIO reservation status
*/
typedef struct gpio_drv_config {
    bool _init_completed;       /*!< Initialization complete                */
    uint64_t _reserve_status;   /*!< GPIO driver reservation state          */
    uint64_t _system_reserved;  /*!< GPIO driver system reservation state   */
    gpio_dev_t *_hw;            /*!< HAL device driver                      */
} gpio_drv_config_t;

static gpio_drv_config_t _drv_config = (gpio_drv_config_t){false, 0LLU, 0LLU, &GPIO };

void gpio_drv_init(void) {
    if(_drv_config._init_completed) return;
    for(uint8_t gpio = GPIO_NUM_0; gpio < SOC_GPIO_PIN_COUNT; gpio++) {_drv_config._system_reserved |= (esp_gpio_is_pin_reserved(gpio)) ? BIT64(gpio) : 0LLU;}
    esp_chip_info_t *chip_info = (esp_chip_info_t *)malloc(sizeof(esp_chip_info_t));
    esp_chip_info(chip_info);
    #ifdef CONFIG_IDF_TARGET_ESP32C6
        _drv_config._system_reserved |= (!(CHIP_FEATURE_EMB_FLASH & chip_info->features) || (CHIP_FEATURE_EMB_PSRAM & chip_info->features)) ? (BIT64(GPIO_NUM_10) | BIT64(GPIO_NUM_11)) : BIT64(GPIO_NUM_14);
    #endif
    #ifdef CONFIG_IDF_TARGET_ESP32
        _drv_config._system_reserved |= 0xF1100000LLU;
    #endif
    _drv_config._reserve_status = _drv_config._system_reserved;
    free(chip_info);
    ESP_LOGI("gdrv", "%llX", _drv_config._reserve_status);
}

bool gpio_drv_reserve(gpio_num_t gpio_num) {
    return gpio_drv_is_pin_reserved(gpio_num) ? false : gpio_drv_reserve_pins(BIT64(gpio_num));
}

bool gpio_drv_reserve_pins(uint64_t pins_mask) {
    pins_mask &= (BIT64(SOC_GPIO_PIN_COUNT) - 1);
    if(!!( _drv_config._reserve_status & pins_mask)) return false;
    _drv_config._reserve_status |= pins_mask;
    return true;
}

bool gpio_drv_free(gpio_num_t gpio_num) {
    return gpio_drv_free_pins(BIT64(gpio_num));
}

bool gpio_drv_free_pins(uint64_t pins_mask) {
    pins_mask &= (BIT64(SOC_GPIO_PIN_COUNT) - 1);
    if(!!(_drv_config._system_reserved & pins_mask)) return false;
    _drv_config._reserve_status &= ~pins_mask;
    return true;
}

bool gpio_drv_is_pin_reserved(gpio_num_t gpio_num)
{
    if (gpio_num >= SOC_GPIO_PIN_COUNT) {
        return false;
    }
    return !!( _drv_config._reserve_status & BIT64(gpio_num));
}

uint64_t gpio_drv_get_reservations(void) {
    return _drv_config._reserve_status;
}

void gpio_drv_get_pin_io_config(gpio_pin_io_config_t *pin_io_config) {
    uint32_t reg_rtcio_value = 0;
    pin_io_config->rtc_mux = false;
    if(rtc_gpio_is_valid_gpio(pin_io_config->gpio_num)) {
        #ifdef CONFIG_IDF_TARGET_ESP32
        reg_rtcio_value = READ_PERI_REG(rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].reg);
        pin_io_config->rtc_mux = !!(reg_rtcio_value & rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].mux);
        //!!(GET_PERI_REG_MASK(rtc_io_desc[rtc_io_num_map[tested_pin]].reg, rtc_io_desc[rtc_io_num_map[tested_pin]].mux));
        #endif /* CONFIG_IDF_TARGET_ESP32 */
        #ifdef CONFIG_IDF_TARGET_ESP32C6
        reg_rtcio_value = (1UL << rtc_io_num_map[pin_io_config->gpio_num]);
        pin_io_config->rtc_mux = !!(HAL_FORCE_READ_U32_REG_FIELD(LP_AON.gpio_mux, gpio_mux_sel) & reg_rtcio_value);
        #endif /* CONFIG_IDF_TARGET_ESP32C6 */
    }
    if(pin_io_config->rtc_mux) {
        #ifdef CONFIG_IDF_TARGET_ESP32
        pin_io_config->open_drain = !!((RTCIO.pin[rtc_io_num_map[pin_io_config->gpio_num]].val >> 2) & 1);
        pin_io_config->input_enable = reg_rtcio_value & rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].ie;
        pin_io_config->output_enable = (READ_PERI_REG(RTC_GPIO_ENABLE_REG) >> 14) & (1UL << rtc_io_num_map[pin_io_config->gpio_num]);
        pin_io_config->pull_up = reg_rtcio_value & rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].pullup;
        pin_io_config->pull_down = reg_rtcio_value & rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].pulldown;
        pin_io_config->function_selected = (reg_rtcio_value >> rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].func) & 3UL;
        pin_io_config->drive_current = (reg_rtcio_value >> rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].drv_s) & rtc_io_desc[rtc_io_num_map[pin_io_config->gpio_num]].drv_s;
        #endif /* CONFIG_IDF_TARGET_ESP32 */
        #ifdef CONFIG_IDF_TARGET_ESP32C6
        pin_io_config->open_drain = !!((uint32_t)LP_IO.pin[rtc_io_num_map[pin_io_config->gpio_num]].pad_driver);
        pin_io_config->input_enable = !!((uint32_t)LP_IO.gpio[rtc_io_num_map[pin_io_config->gpio_num]].fun_ie);
        pin_io_config->output_enable = !!(HAL_FORCE_READ_U32_REG_FIELD(LP_IO.out_enable, enable) & reg_rtcio_value);
        pin_io_config->pull_up = !!((uint32_t)LP_IO.gpio[rtc_io_num_map[pin_io_config->gpio_num]].fun_wpu);
        pin_io_config->pull_down = !!((uint32_t)LP_IO.gpio[rtc_io_num_map[pin_io_config->gpio_num]].fun_wpd);
        pin_io_config->function_selected = (uint32_t)LP_IO.gpio[rtc_io_num_map[pin_io_config->gpio_num]].mcu_sel;
        pin_io_config->drive_current = (uint32_t)LP_IO.gpio[rtc_io_num_map[pin_io_config->gpio_num]].fun_drv;
        #endif /* CONFIG_IDF_TARGET_ESP32C6 */
    } else {
        gpio_ll_get_io_config(_drv_config._hw, pin_io_config->gpio_num, &(pin_io_config->pull_up), &(pin_io_config->pull_down), &(pin_io_config->input_enable), &(pin_io_config->output_enable),
                        &(pin_io_config->open_drain), &(pin_io_config->drive_current), &(pin_io_config->function_selected), &(pin_io_config->signal_out), &(pin_io_config->sleep_enable));
    }
    pin_io_config->reserved = gpio_drv_is_pin_reserved(pin_io_config->gpio_num);
}

uint32_t gpio_drv_get_in_signal(gpio_num_t gpio_num) {
    uint32_t signal = 0;
    while((signal<SIG_GPIO_OUT_IDX) && (gpio_ll_get_in_signal_connected_io(_drv_config._hw, signal) != gpio_num)) {
        signal++;
    }
    return signal;
}

#ifdef CONFIG_GPIO_TEXT_FUNCTIONS
char *gpio_drv_get_sig_name(uint32_t signal, bool is_input) {
    if(signal < SIG_GPIO_OUT_IDX ) return (is_input) ? (char *)cc_gpio_matrix_sig[0][signal] : (char *)cc_gpio_matrix_sig[1][signal];
    return (char *)pure_gpio;
}

char *gpio_drv_get_iomux_func_name(uint32_t function, gpio_num_t gpio_num) {
    if(gpio_num < GPIO_NUM_MAX) {
        if(PIN_FUNC_GPIO == function) return (char *)pure_gpio;
        function -= (function > PIN_FUNC_GPIO);
        return (char *)cc_iomux_func[function][gpio_num];
    }
    return (char *)error_gpio;
}

char *gpio_drv_get_io_description(gpio_num_t gpio_num, bool short_desc) {
    char *description = (char *)calloc(short_desc ? 128 : 256, sizeof(char));
    gpio_pin_io_config_t *pin_io = (gpio_pin_io_config_t *)malloc(sizeof(gpio_pin_io_config_t));
    pin_io->gpio_num = gpio_num;
    gpio_drv_get_pin_io_config(pin_io);
    
    if(pin_io->reserved) {
        if(pin_io->rtc_mux) {
            sprintf(description, (short_desc)?"IO%02d RSVD %s%s%s%s%s%s%s%s%s DC:%dmA %s":"IO%02d is reserved %s%s%s%s%s%s%s%s%s with Drive current %dmA via %s IOMUX",
                pin_io->gpio_num,
                (pin_io->open_drain)?(short_desc)?"OD:":"Open drain ":"",
                (pin_io->input_enable)?(short_desc)?"In":"Input":"",(pin_io->input_enable)&&(pin_io->output_enable)?"/":"",(pin_io->output_enable)?(short_desc)?"Out":"Output":"",
                (pin_io->input_enable)||(pin_io->output_enable)?(short_desc)?":":" ":"",
                (pin_io->pull_up)?(short_desc)?"PU":"PullUp":"",(pin_io->pull_up)&&(pin_io->pull_down)?"/":"",(pin_io->pull_down)?(short_desc)?"PD":"PullDown":"",
                (!pin_io->pull_up)&&(!pin_io->pull_down)?(short_desc)?"HI":"HighImpedance":"", 5*(1<<pin_io->drive_current), cc_iomux_name
            );
        } else { 
            sprintf(description, (short_desc)?"IO%02d RSVD %s%s%s%s%s%s%s%s%s%s DC:%dmA %s %s%s%s%s%s%s":"IO%02d is reserved %s%s%s%s%s%s%s%s%s%s with Drive current %dmA via %s %s%s%s%s%s%s",
                pin_io->gpio_num,
                (pin_io->open_drain)?(short_desc)?"OD:":"Open drain ":"",
                (pin_io->input_enable)?(short_desc)?"In":"Input":"",(pin_io->input_enable)&&(pin_io->output_enable)?"/":"",(pin_io->output_enable)?(short_desc)?"Out":"Output":"",
                (pin_io->input_enable)||(pin_io->output_enable)?(short_desc)?":":" ":"",
                (pin_io->pull_up)?(short_desc)?"PU":"PullUp":"",(pin_io->pull_up)&&(pin_io->pull_down)?"/":"",(pin_io->pull_down)?(short_desc)?"PD":"PullDown":"",
                (!pin_io->pull_up)&&(!pin_io->pull_down)?(short_desc)?"HI":"HighImpedance":"", 
                (pin_io->sleep_enable)?(short_desc)?"[SSEn]":" (SSEn)":"", 5*(1<<pin_io->drive_current),
                (pin_io->function_selected == PIN_FUNC_GPIO) ? "GPIO Matrix" : "IOMUX",
                (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)?"SigIn:":"",
                (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)?gpio_drv_get_sig_name(gpio_drv_get_in_signal(pin_io->gpio_num), true):"",
                (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)&&(pin_io->output_enable)?(short_desc)?" ":" and ":"",
                (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->output_enable)?"SigOut:":"",
                (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->output_enable)?gpio_drv_get_sig_name(pin_io->signal_out, false):"",
                (pin_io->function_selected != PIN_FUNC_GPIO)?gpio_drv_get_iomux_func_name(pin_io->function_selected ,pin_io->gpio_num):""
            );
        }
    } else sprintf(description, "IO%02d is free", pin_io->gpio_num);
    
    free(pin_io);
    return realloc(description, strlen(description)+1);
}
#endif /* CONFIG_GPIO_TEXT_FUNCTIONS */
/*
if(pin_io->reserved) {
    ESP_LOGE("tst", 
        "IO%02d RSVD %s%s%s%s:%s%s DC:%dmA %s %s%s%s%s%s%s",
        pin_io->gpio_num,
        (pin_io->open_drain)?"OD:":"",
        (pin_io->input_enable)?"In":"",(pin_io->input_enable)&&(pin_io->output_enable)?"/":"",(pin_io->output_enable)?"Out":"",
        (pin_io->pull_up)?"PU":(pin_io->pull_down)?"PD":"HI", (pin_io->sleep_enable)?"[SSEn]":"", 5*(1<<pin_io->drive_current),
        (pin_io->function_selected == PIN_FUNC_GPIO) ? "GPIO Matrix" : "IOMUX",
        (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)?"SigIn:":"",
        (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)?gpio_drv_get_sig_name(gpio_drv_get_in_signal(pin_io->gpio_num), true):"",
        (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->input_enable)&&(pin_io->output_enable)?" ":"",
        (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->output_enable)?"SigOut:":"",
        (pin_io->function_selected == PIN_FUNC_GPIO)&&(pin_io->output_enable)?gpio_drv_get_sig_name(pin_io->signal_out, false):"",
        (pin_io->function_selected != PIN_FUNC_GPIO)?gpio_drv_get_iomux_func_name(pin_io->function_selected ,pin_io->gpio_num):""
    );
} else ESP_LOGE("tst", "IO%02d is free", pin_io->gpio_num);
*/