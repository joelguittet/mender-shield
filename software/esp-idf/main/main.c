/**
 * @file      main.c
 * @brief     Main entry point
 *
 * Copyright joelguittet and mender-mcu-client contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <driver/i2c.h>
#include <esp_event.h>
#ifdef CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER
#include <esp_littlefs.h>
#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER */
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT */
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include "mender-client.h"
#include "mender-configure.h"
#include "mender-flash.h"
#include "mender-inventory.h"
#include "mender-troubleshoot.h"
#include <nvs_flash.h>
#include <protocol_examples_common.h>
#include <regex.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include <time.h>

/**
 * @brief Tag used for logging
 */
static const char *TAG = "main";

/**
 * @brief Mender client events
 */
static EventGroupHandle_t mender_client_events;
#define MENDER_CLIENT_EVENT_CONNECT      (1 << 0)
#define MENDER_CLIENT_EVENT_CONNECTED    (1 << 1)
#define MENDER_CLIENT_EVENT_DISCONNECT   (1 << 2)
#define MENDER_CLIENT_EVENT_DISCONNECTED (1 << 3)
#define MENDER_CLIENT_EVENT_RESTART      (1 << 4)

/**
 * @brief Mender shield events
 */
static EventGroupHandle_t mender_shield_events;
#define MENDER_SHIELD_EVENT_CONNECTING    (1 << 0)
#define MENDER_SHIELD_EVENT_CONNECTED     (1 << 1)
#define MENDER_SHIELD_EVENT_DISCONNECTING (1 << 2)
#define MENDER_SHIELD_EVENT_DISCONNECTED  (1 << 3)
#define MENDER_SHIELD_EVENT_AUTHENTICATED (1 << 4)
#define MENDER_SHIELD_EVENT_DOWNLOADING   (1 << 5)
#define MENDER_SHIELD_EVENT_INSTALLING    (1 << 6)
#define MENDER_SHIELD_EVENT_REBOOTING     (1 << 7)
#define MENDER_SHIELD_EVENT_SUCCESS       (1 << 8)
#define MENDER_SHIELD_EVENT_FAILURE       (1 << 9)

/**
 * @brief Mender shield I2C settings
 */
#define MENDER_SHIELD_I2C_MASTER_NUM            0
#define MENDER_SHIELD_I2C_MASTER_SCL_IO         22
#define MENDER_SHIELD_I2C_MASTER_SDA_IO         21
#define MENDER_SHIELD_I2C_MASTER_SDA_PULLUP     GPIO_PULLUP_ENABLE
#define MENDER_SHIELD_I2C_MASTER_SCL_PULLUP     GPIO_PULLUP_ENABLE
#define MENDER_SHIELD_I2C_MASTER_FREQ_HZ        100000
#define MENDER_SHIELD_I2C_MASTER_TX_BUF_DISABLE 0
#define MENDER_SHIELD_I2C_MASTER_RX_BUF_DISABLE 0
#define MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS     1000

/**
 * @brief Mender shield I2C addresses
 */
#define MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR  0x68
#define MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR 0x69

/**
 * @brief Mender shield I2C registers
 */
#define MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER 0x02

/**
 * @brief Mender shield background colors
 */
static const uint8_t MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_RED[]
    = { MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER, 0x80, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80 };
static const uint8_t MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN[]
    = { MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER, 0x80, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80 };
static const uint8_t MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_BLUE[]
    = { MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80 };

/**
 * @brief Mender shield logo colors
 */
static const uint8_t MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE[]
    = { MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER, 0x42, 0x5D, 0x0F, 0x43, 0x01, 0x59, 0x69, 0x88, 0xF0, 0xF8, 0xF0, 0x88, 0xF0 };
static const uint8_t MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NORMAL_MODE[]
    = { MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER, 0x82, 0x5D, 0x0F, 0x43, 0x01, 0x59, 0x69, 0x88, 0xF0, 0xF8, 0xF0, 0x88, 0xF0 };

/**
 * @brief Network connnect callback
 * @return MENDER_OK if network is connected following the request, error code otherwise
 */
static mender_err_t
network_connect_cb(void) {

    ESP_LOGI(TAG, "Mender client connect network");

    /* This callback can be used to configure network connection */
    /* Note that the application can connect the network before if required */
    /* This callback only indicates the mender-client requests network access now */
    /* In this example, example_connect and example_disconnect configures Wi-Fi or Ethernet, as selected in menuconfig */
    /* Read "Establishing Wi-Fi or Ethernet Connection" section in examples/protocols/README.md for more information */
    xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECT);
    EventBits_t bits = xEventGroupWaitBits(
        mender_client_events, MENDER_CLIENT_EVENT_CONNECTED | MENDER_CLIENT_EVENT_DISCONNECTED, pdTRUE, pdFALSE, 30000 / portTICK_PERIOD_MS);
    if (MENDER_CLIENT_EVENT_CONNECTED != (bits & MENDER_CLIENT_EVENT_CONNECTED)) {
        return MENDER_FAIL;
    }

    return MENDER_OK;
}

/**
 * @brief Network release callback
 * @return MENDER_OK if network is released following the request, error code otherwise
 */
static mender_err_t
network_release_cb(void) {

    ESP_LOGI(TAG, "Mender client released network");

    /* This callback can be used to release network connection */
    /* Note that the application can keep network activated if required */
    /* This callback only indicates the mender-client doesn't request network access now */
    xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_DISCONNECT);

    return MENDER_OK;
}

/**
 * @brief Authentication success callback
 * @return MENDER_OK if application is marked valid and success deployment status should be reported to the server, error code otherwise
 */
static mender_err_t
authentication_success_cb(void) {

    mender_err_t ret;

    ESP_LOGI(TAG, "Mender client authenticated");
    xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_AUTHENTICATED);

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT
    /* Activate troubleshoot add-on (deactivated by default) */
    if (MENDER_OK != (ret = mender_troubleshoot_activate())) {
        ESP_LOGE(TAG, "Unable to activate troubleshoot add-on");
        return ret;
    }
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT */

    /* Validate the image if it is still pending */
    /* Note it is possible to do multiple diagnosic tests before validating the image */
    /* In this example, authentication success with the mender-server is enough */
    if (MENDER_OK != (ret = mender_flash_confirm_image())) {
        ESP_LOGE(TAG, "Unable to validate the image");
        return ret;
    }

    return ret;
}

/**
 * @brief Authentication failure callback
 * @return MENDER_OK if nothing to do, error code if the mender client should restart the application
 */
static mender_err_t
authentication_failure_cb(void) {

    static int tries = 0;

    /* Check if confirmation of the image is still pending */
    if (true == mender_flash_is_image_confirmed()) {
        ESP_LOGI(TAG, "Mender client authentication failed");
        return MENDER_OK;
    }

    /* Increment number of failures */
    tries++;
    ESP_LOGE(TAG, "Mender client authentication failed (%d/%d)", tries, CONFIG_EXAMPLE_AUTHENTICATION_FAILS_MAX_TRIES);
    xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_FAILURE);

    /* Restart the application after several authentication failures with the mender-server */
    /* The image has not been confirmed and the bootloader will now rollback to the previous working image */
    /* Note it is possible to customize this depending of the wanted behavior */
    return (tries >= CONFIG_EXAMPLE_AUTHENTICATION_FAILS_MAX_TRIES) ? MENDER_FAIL : MENDER_OK;
}

/**
 * @brief Deployment status callback
 * @param status Deployment status value
 * @param desc Deployment status description as string
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
deployment_status_cb(mender_deployment_status_t status, char *desc) {

    /* We can do something else if required */
    ESP_LOGI(TAG, "Deployment status is '%s'", desc);

    /* Raise mender-shield event depending of the status */
    switch (status) {
        case MENDER_DEPLOYMENT_STATUS_DOWNLOADING:
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_DOWNLOADING);
            break;
        case MENDER_DEPLOYMENT_STATUS_INSTALLING:
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_INSTALLING);
            break;
        case MENDER_DEPLOYMENT_STATUS_REBOOTING:
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_REBOOTING);
            break;
        case MENDER_DEPLOYMENT_STATUS_SUCCESS:
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_SUCCESS);
            break;
        case MENDER_DEPLOYMENT_STATUS_FAILURE:
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_FAILURE);
            break;
        default:
            break;
    }

    return MENDER_OK;
}

/**
 * @brief Restart callback
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
restart_cb(void) {

    /* Application is responsible to shutdown and restart the system now */
    xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_RESTART);

    return MENDER_OK;
}

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE
#ifndef CONFIG_MENDER_CLIENT_CONFIGURE_STORAGE

/**
 * @brief Device configuration updated
 * @param configuration Device configuration
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
config_updated_cb(mender_keystore_t *configuration) {

    /* Application can use the new device configuration now */
    /* In this example, we just print the content of the configuration received from the Mender server */
    if (NULL != configuration) {
        size_t index = 0;
        ESP_LOGI(TAG, "Device configuration received from the server");
        while ((NULL != configuration[index].name) && (NULL != configuration[index].value)) {
            ESP_LOGI(TAG, "Key=%s, value=%s", configuration[index].name, configuration[index].value);
            index++;
        }
    }

    return MENDER_OK;
}

#endif /* CONFIG_MENDER_CLIENT_CONFIGURE_STORAGE */
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE */

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER

static mender_err_t
file_transfer_stat_cb(char *path, size_t **size, uint32_t **uid, uint32_t **gid, uint32_t **mode, time_t **time) {

    assert(NULL != path);
    struct stat  stats;
    mender_err_t ret = MENDER_OK;

    /* Get statistics of file */
    if (0 != stat(path, &stats)) {
        ESP_LOGE(TAG, "Unable to get statistics of file '%s'", path);
        ret = MENDER_FAIL;
        goto FAIL;
    }
    /* Size is optional */
    if (NULL != size) {
        if (NULL == (*size = (size_t *)malloc(sizeof(size_t)))) {
            ESP_LOGE(TAG, "Unable to allocate memory");
            ret = MENDER_FAIL;
            goto FAIL;
        }
        **size = stats.st_size;
    }
    /* UID and GID are optional */
    if (NULL != uid) {
        if (NULL == (*uid = (uint32_t *)malloc(sizeof(uint32_t)))) {
            ESP_LOGE(TAG, "Unable to allocate memory");
            ret = MENDER_FAIL;
            goto FAIL;
        }
        **uid = stats.st_uid;
    }
    if (NULL != gid) {
        if (NULL == (*gid = (uint32_t *)malloc(sizeof(uint32_t)))) {
            ESP_LOGE(TAG, "Unable to allocate memory");
            ret = MENDER_FAIL;
            goto FAIL;
        }
        **gid = stats.st_gid;
    }
    /* Mode is not optional and file must be a regular file to be downloaded by the server */
    if (NULL != mode) {
        if (NULL == (*mode = (uint32_t *)malloc(sizeof(uint32_t)))) {
            ESP_LOGE(TAG, "Unable to allocate memory");
            ret = MENDER_FAIL;
            goto FAIL;
        }
        **mode = stats.st_mode;
    }
    /* Last modification time is optional, format seconds since epoch */
    if (NULL != time) {
        if (NULL == (*time = (time_t *)malloc(sizeof(time_t)))) {
            ESP_LOGE(TAG, "Unable to allocate memory");
            ret = MENDER_FAIL;
            goto FAIL;
        }
        **time = stats.st_mtim.tv_sec;
    }

FAIL:

    return ret;
}

static mender_err_t
file_transfer_open_cb(char *path, char *mode, void **handle) {

    assert(NULL != path);
    assert(NULL != mode);

    /* Open file */
    ESP_LOGI(TAG, "Opening file '%s' with mode '%s'", path, mode);
    if (NULL == (*handle = (void *)fopen(path, mode))) {
        ESP_LOGE(TAG, "Unable to open file '%s'", path);
        return MENDER_FAIL;
    }

    return MENDER_OK;
}

static mender_err_t
file_transfer_read_cb(void *handle, void *data, size_t *length) {

    assert(NULL != handle);
    assert(NULL != data);
    assert(NULL != length);

    /* Read file */
    *length = fread(data, sizeof(uint8_t), *length, (FILE *)handle);

    return MENDER_OK;
}

static mender_err_t
file_transfer_write_cb(void *handle, void *data, size_t length) {

    assert(NULL != handle);
    assert(NULL != data);

    /* Write file */
    if (length != fwrite(data, sizeof(uint8_t), length, (FILE *)handle)) {
        ESP_LOGE(TAG, "Unable to write data to the file");
        return MENDER_FAIL;
    }

    return MENDER_OK;
}

static mender_err_t
file_transfer_close_cb(void *handle) {

    assert(NULL != handle);

    /* Close file */
    ESP_LOGI(TAG, "Closing file");
    fclose((FILE *)handle);

    return MENDER_OK;
}

#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER */
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_SHELL

/**
 * @brief Function used to replace a string in the input buffer
 * @param input Input buffer
 * @param search String to be replaced or regex expression
 * @param replace Replacement string
 * @return New string with replacements if the function succeeds, NULL otherwise
 */
static char *
str_replace(char *input, char *search, char *replace) {

    assert(NULL != input);
    assert(NULL != search);
    assert(NULL != replace);

    regex_t    regex;
    regmatch_t match;
    char      *str                   = input;
    char      *output                = NULL;
    size_t     index                 = 0;
    int        previous_match_finish = 0;

    /* Compile expression */
    if (0 != regcomp(&regex, search, REG_EXTENDED)) {
        /* Unable to compile expression */
        ESP_LOGE(TAG, "Unable to compile expression '%s'", search);
        return NULL;
    }

    /* Loop until all search string are replaced */
    bool loop = true;
    while (true == loop) {

        /* Search wanted string */
        if (0 != regexec(&regex, str, 1, &match, 0)) {
            /* No more string to be replaced */
            loop = false;
        } else {
            if (match.rm_so != -1) {

                /* Beginning and ending offset of the match */
                int current_match_start  = (int)(match.rm_so + (str - input));
                int current_match_finish = (int)(match.rm_eo + (str - input));

                /* Reallocate output memory */
                char *tmp = (char *)realloc(output, index + (current_match_start - previous_match_finish) + 1);
                if (NULL == tmp) {
                    ESP_LOGE(TAG, "Unable to allocate memory");
                    regfree(&regex);
                    free(output);
                    return NULL;
                }
                output = tmp;

                /* Copy string from previous match to the beginning of the current match */
                memcpy(&output[index], &input[previous_match_finish], current_match_start - previous_match_finish);
                index += (current_match_start - previous_match_finish);
                output[index] = 0;

                /* Reallocate output memory */
                if (NULL == (tmp = (char *)realloc(output, index + strlen(replace) + 1))) {
                    ESP_LOGE(TAG, "Unable to allocate memory");
                    regfree(&regex);
                    free(output);
                    return NULL;
                }
                output = tmp;

                /* Copy replace string to the output */
                strcat(output, replace);
                index += strlen(replace);

                /* Update previous match ending value */
                previous_match_finish = current_match_finish;
            }
            str += match.rm_eo;
        }
    }

    /* Reallocate output memory */
    char *tmp = (char *)realloc(output, index + (strlen(input) - previous_match_finish) + 1);
    if (NULL == tmp) {
        ESP_LOGE(TAG, "Unable to allocate memory");
        regfree(&regex);
        free(output);
        return NULL;
    }
    output = tmp;

    /* Copy the end of the string after the latest match */
    memcpy(&output[index], &input[previous_match_finish], strlen(input) - previous_match_finish);
    index += (strlen(input) - previous_match_finish);
    output[index] = 0;

    /* Release regex */
    regfree(&regex);

    return output;
}

/**
 * @brief Shell vprintf function used to route logs
 * @param format Log format string
 * @param args Log arguments list
 * @return Length of the log
 */
static int
shell_vprintf(const char *format, va_list args) {

    assert(NULL != format);
    char *buffer, *tmp;
    char  data[256];
    int   length;

    /* Format the log */
    length = vsnprintf(data, sizeof(data), format, args);
    if (length > sizeof(data) - 1) {
        data[sizeof(data) - 1] = '\0';
    }

    /* Ensure new line is "\r\n" to have a proper display of the data in the shell */
    if (NULL == (buffer = strndup(data, length))) {
        goto END;
    }
    if (NULL == (tmp = str_replace(buffer, "\r|\n", "\r\n"))) {
        goto END;
    }
    free(buffer);
    buffer = tmp;

    /* Print log on the shell */
    mender_troubleshoot_shell_print((uint8_t *)buffer, strlen(buffer));

END:

    /* Release memory */
    if (NULL != buffer) {
        free(buffer);
    }

    return length;
}

/**
 * @brief Shell open callback
 * @param terminal_width Terminal width
 * @param terminal_height Terminal height
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
shell_open_cb(uint16_t terminal_width, uint16_t terminal_height) {

    /* Shell is connected, print terminal size */
    ESP_LOGI(TAG, "Shell connected with width=%d and height=%d", terminal_width, terminal_height);

    /* Route logs (ESP_LOGx) to the shell */
    esp_log_set_vprintf(shell_vprintf);

    return MENDER_OK;
}

/**
 * @brief Shell resize callback
 * @param terminal_width Terminal width
 * @param terminal_height Terminal height
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
shell_resize_cb(uint16_t terminal_width, uint16_t terminal_height) {

    /* Just print terminal size */
    ESP_LOGI(TAG, "Shell resized with width=%d and height=%d", terminal_width, terminal_height);

    return MENDER_OK;
}

/**
 * @brief Shell write data callback
 * @param data Shell data received
 * @param length Length of the data received
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
shell_write_cb(void *data, size_t length) {

    mender_err_t ret = MENDER_OK;
    char        *buffer, *tmp;

    /* Ensure new line is "\r\n" to have a proper display of the data in the shell */
    if (NULL == (buffer = (char *)malloc(length + 1))) {
        ESP_LOGE(TAG, "Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    memcpy(buffer, data, length);
    buffer[length] = '\0';
    if (NULL == (tmp = str_replace(buffer, "\r|\n", "\r\n"))) {
        ESP_LOGE(TAG, "Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    free(buffer);
    buffer = tmp;

    /* Send back the data received */
    if (MENDER_OK != (ret = mender_troubleshoot_shell_print((void *)buffer, strlen(buffer)))) {
        ESP_LOGE(TAG, "Unable to print data to the shell");
        ret = MENDER_FAIL;
        goto END;
    }

END:

    /* Release memory */
    if (NULL != buffer) {
        free(buffer);
    }

    return ret;
}

/**
 * @brief Shell close callback
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t
shell_close_cb(void) {

    /* Route logs back to the UART port */
    esp_log_set_vprintf(vprintf);

    /* Shell has been disconnected */
    ESP_LOGI(TAG, "Shell disconnected");

    return MENDER_OK;
}

#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_SHELL */
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT */

/**
 * @brief Initialize mender-shield
 * @return ESP_OK if the function succeeds, error code otherwise
 */
static esp_err_t
shield_init(void) {

    esp_err_t ret;

    /* Initialize I2C bus */
    i2c_config_t i2c_config = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = MENDER_SHIELD_I2C_MASTER_SDA_IO,
        .scl_io_num       = MENDER_SHIELD_I2C_MASTER_SCL_IO,
        .sda_pullup_en    = MENDER_SHIELD_I2C_MASTER_SDA_PULLUP,
        .scl_pullup_en    = MENDER_SHIELD_I2C_MASTER_SCL_PULLUP,
        .master.clk_speed = MENDER_SHIELD_I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(MENDER_SHIELD_I2C_MASTER_NUM, &i2c_config);
    if (ESP_OK
        != (ret = i2c_driver_install(
                MENDER_SHIELD_I2C_MASTER_NUM, i2c_config.mode, MENDER_SHIELD_I2C_MASTER_RX_BUF_DISABLE, MENDER_SHIELD_I2C_MASTER_TX_BUF_DISABLE, 0))) {
        ESP_LOGE(TAG, "Unable to install I2C driver");
        return ret;
    }

    return ret;
}

/**
 * @brief Release mender-shield
 * @return ESP_OK if the function succeeds, error code otherwise
 */
static esp_err_t
shield_exit(void) {

    esp_err_t ret;

    /* Release I2C bus */
    if (ESP_OK != (ret = i2c_driver_delete(MENDER_SHIELD_I2C_MASTER_NUM))) {
        ESP_LOGE(TAG, "Unable to release I2C driver");
        return ret;
    }

    return ret;
}

/**
 * @brief Task mender-shield
 * @param params Task parameters (unused)
 */
static void
shield_task(void *params) {

    /* Initialize shield with green background and Mender logo displayed with night mode */
    assert(ESP_OK
           == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                         MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                         MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN,
                                         sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                         MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
    assert(ESP_OK
           == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                         MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                         MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                         sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                         MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));

    /* Wait for mender-shield events, update mender-shield display according to the device status */
    TickType_t delay = portMAX_DELAY;
    while (1) {

        /* Wait for mender-shield events */
        EventBits_t event = xEventGroupWaitBits(mender_shield_events, UINT16_MAX, pdTRUE, pdFALSE, delay);

        /* Treatment depending of the event */
        if (MENDER_SHIELD_EVENT_FAILURE == (event & MENDER_SHIELD_EVENT_FAILURE)) {

            /* Shield with red background and Mender logo displayed with night mode */
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_RED,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_CONNECTING == (event & MENDER_SHIELD_EVENT_CONNECTING)) {

            /* Nothing to do */
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_CONNECTED == (event & MENDER_SHIELD_EVENT_CONNECTED)) {

            /* Shield with blue background and Mender logo displayed with night mode */
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_BLUE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_DISCONNECTING == (event & MENDER_SHIELD_EVENT_DISCONNECTING)) {

            /* Nothing to do */
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_DISCONNECTED == (event & MENDER_SHIELD_EVENT_DISCONNECTED)) {

            /* Shield with green background and Mender logo displayed with night mode */
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_AUTHENTICATED == (event & MENDER_SHIELD_EVENT_AUTHENTICATED)) {

            /* Shield with blue background and Mender logo displayed with normal mode */
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_BLUE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NORMAL_MODE,
                                                 sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NORMAL_MODE),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            delay = portMAX_DELAY;

        } else if (MENDER_SHIELD_EVENT_DOWNLOADING == (event & MENDER_SHIELD_EVENT_DOWNLOADING)) {

            /* Mender logo blinking slowly during the download */
            delay = 1000 / portTICK_PERIOD_MS;

        } else if (MENDER_SHIELD_EVENT_INSTALLING == (event & MENDER_SHIELD_EVENT_INSTALLING)) {

            /* Mender logo blinking rapidly during the installation */
            delay = 250 / portTICK_PERIOD_MS;

        } else if (MENDER_SHIELD_EVENT_REBOOTING == (event & MENDER_SHIELD_EVENT_REBOOTING)) {

            /* Exit the shield task */
            goto END;

        } else if (MENDER_SHIELD_EVENT_SUCCESS == (event & MENDER_SHIELD_EVENT_SUCCESS)) {

            /* Nothing to do */
            delay = portMAX_DELAY;

        } else {

            /* Mender logo blinking */
            uint8_t              mode = 0x00;
            static const uint8_t reg  = MENDER_SHIELD_I2C_LED_DRIVER_CONTROL_REGISTER;
            assert(ESP_OK
                   == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                 MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                 &reg,
                                                 sizeof(uint8_t),
                                                 MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            assert(ESP_OK
                   == i2c_master_read_from_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                  MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                  &mode,
                                                  sizeof(uint8_t),
                                                  MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            if (0x80 == (mode & 0xC0)) {
                assert(ESP_OK
                       == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                     MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                     MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                                     sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                                     MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            } else {
                assert(ESP_OK
                       == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                                     MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                                     MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NORMAL_MODE,
                                                     sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NORMAL_MODE),
                                                     MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
            }
        }
    }

END:

    /* Shield with green background and Mender logo displayed with night mode */
    assert(ESP_OK
           == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                         MENDER_SHIELD_I2C_LED_DRIVER_BACK_ADDR,
                                         MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN,
                                         sizeof(MENDER_SHIELD_I2C_LED_DRIVER_BACKGROUND_GREEN),
                                         MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));
    assert(ESP_OK
           == i2c_master_write_to_device(MENDER_SHIELD_I2C_MASTER_NUM,
                                         MENDER_SHIELD_I2C_LED_DRIVER_FRONT_ADDR,
                                         MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE,
                                         sizeof(MENDER_SHIELD_I2C_LED_DRIVER_FRONT_LOGO_NIGHT_MODE),
                                         MENDER_SHIELD_I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));

    /* Delete myself */
    vTaskDelete(NULL);
}

/**
 * @brief Main function
 */
void
app_main(void) {

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if ((ESP_ERR_NVS_NO_FREE_PAGES == ret) || (ESP_ERR_NVS_NEW_VERSION_FOUND == ret)) {
        ESP_LOGI(TAG, "Erasing flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER

    /* Initialize LittleFS */
    esp_vfs_littlefs_conf_t littlefs_conf = {
        .base_path              = "/littlefs",
        .partition_label        = "storage",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    ret = esp_vfs_littlefs_register(&littlefs_conf);
    if (ESP_OK != ret) {
        if (ESP_FAIL == ret) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ESP_ERR_NOT_FOUND == ret) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
    }
    ESP_ERROR_CHECK(ret);
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(littlefs_conf.partition_label, &total, &used);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
        esp_littlefs_format(littlefs_conf.partition_label);
    } else {
        ESP_LOGI(TAG, "LittleFS partition size: total: %d, used: %d", total, used);
    }
    ESP_ERROR_CHECK(ret);

#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER */
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT */

    /* Initialize network */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Read base MAC address of the device */
    uint8_t mac[6];
    char    mac_address[18];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    sprintf(mac_address, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "MAC address of the device '%s'", mac_address);

    /* Create mender-client event group */
    mender_client_events = xEventGroupCreate();
    ESP_ERROR_CHECK(NULL == mender_client_events);

    /* Create mender-shield event group */
    mender_shield_events = xEventGroupCreate();
    ESP_ERROR_CHECK(NULL == mender_shield_events);

    /* Initialize mender-shield hardware */
    ESP_ERROR_CHECK(shield_init());

    /* Create mender-shield task */
    ESP_ERROR_CHECK(pdPASS != xTaskCreate(shield_task, "shield_task", 4096, NULL, tskIDLE_PRIORITY, NULL));

    /* Retrieve running version of the device */
    esp_app_desc_t         running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_ERROR_CHECK(esp_ota_get_partition_description(running, &running_app_info));
    ESP_LOGI(TAG, "Running project '%s' version '%s'", running_app_info.project_name, running_app_info.version);

    /* Compute artifact name */
    char artifact_name[128];
    sprintf(artifact_name, "%s-v%s", running_app_info.project_name, running_app_info.version);

    /* Retrieve device type */
    char *device_type = running_app_info.project_name;

    /* Initialize mender-client */
    mender_keystore_t         identity[]              = { { .name = "mac", .value = mac_address }, { .name = NULL, .value = NULL } };
    mender_client_config_t    mender_client_config    = { .identity                     = identity,
                                                          .artifact_name                = artifact_name,
                                                          .device_type                  = device_type,
                                                          .host                         = NULL,
                                                          .tenant_token                 = NULL,
                                                          .authentication_poll_interval = 0,
                                                          .update_poll_interval         = 0,
                                                          .recommissioning              = false };
    mender_client_callbacks_t mender_client_callbacks = { .network_connect        = network_connect_cb,
                                                          .network_release        = network_release_cb,
                                                          .authentication_success = authentication_success_cb,
                                                          .authentication_failure = authentication_failure_cb,
                                                          .deployment_status      = deployment_status_cb,
                                                          .restart                = restart_cb };
    ESP_ERROR_CHECK(mender_client_init(&mender_client_config, &mender_client_callbacks));
    ESP_LOGI(TAG, "Mender client initialized");

    /* Initialize mender add-ons */
#ifdef CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE
    mender_configure_config_t    mender_configure_config    = { .refresh_interval = 0 };
    mender_configure_callbacks_t mender_configure_callbacks = {
#ifndef CONFIG_MENDER_CLIENT_CONFIGURE_STORAGE
        .config_updated = config_updated_cb,
#endif /* CONFIG_MENDER_CLIENT_CONFIGURE_STORAGE */
    };
    ESP_ERROR_CHECK(mender_client_register_addon(
        (mender_addon_instance_t *)&mender_configure_addon_instance, (void *)&mender_configure_config, (void *)&mender_configure_callbacks));
    ESP_LOGI(TAG, "Mender configure add-on registered");
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE */
#ifdef CONFIG_MENDER_CLIENT_ADD_ON_INVENTORY
    mender_inventory_config_t mender_inventory_config = { .refresh_interval = 0 };
    ESP_ERROR_CHECK(mender_client_register_addon((mender_addon_instance_t *)&mender_inventory_addon_instance, (void *)&mender_inventory_config, NULL));
    ESP_LOGI(TAG, "Mender inventory add-on registered");
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_INVENTORY */
#ifdef CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT
    mender_troubleshoot_config_t    mender_troubleshoot_config    = { .host = NULL, .healthcheck_interval = 0 };
    mender_troubleshoot_callbacks_t mender_troubleshoot_callbacks = {
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER
        .file_transfer = { .stat  = file_transfer_stat_cb,
                           .open  = file_transfer_open_cb,
                           .read  = file_transfer_read_cb,
                           .write = file_transfer_write_cb,
                           .close = file_transfer_close_cb },
#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_FILE_TRANSFER */
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_PORT_FORWARDING
        .port_forwarding = { .connect = NULL, .send = NULL, .close = NULL },
#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_PORT_FORWARDING */
#ifdef CONFIG_MENDER_CLIENT_TROUBLESHOOT_SHELL
        .shell = { .open = shell_open_cb, .resize = shell_resize_cb, .write = shell_write_cb, .close = shell_close_cb }
#endif /* CONFIG_MENDER_CLIENT_TROUBLESHOOT_SHELL */
    };
    ESP_ERROR_CHECK(mender_client_register_addon(
        (mender_addon_instance_t *)&mender_troubleshoot_addon_instance, (void *)&mender_troubleshoot_config, (void *)&mender_troubleshoot_callbacks));
    ESP_LOGI(TAG, "Mender troubleshoot add-on registered");
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_TROUBLESHOOT */

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE
    /* Get mender configuration (this is just an example to illustrate the API) */
    mender_keystore_t *configuration;
    if (MENDER_OK != mender_configure_get(&configuration)) {
        ESP_LOGE(TAG, "Unable to get mender configuration");
    } else if (NULL != configuration) {
        size_t index = 0;
        ESP_LOGI(TAG, "Device configuration retrieved");
        while ((NULL != configuration[index].name) && (NULL != configuration[index].value)) {
            ESP_LOGI(TAG, "Key=%s, value=%s", configuration[index].name, configuration[index].value);
            index++;
        }
        mender_utils_keystore_delete(configuration);
    }
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_CONFIGURE */

#ifdef CONFIG_MENDER_CLIENT_ADD_ON_INVENTORY
    /* Set mender inventory (this is just an example to illustrate the API) */
    mender_keystore_t inventory[] = { { .name = "esp-idf", .value = IDF_VER },
                                      { .name = "mender-mcu-client", .value = mender_client_version() },
                                      { .name = "latitude", .value = "45.8325" },
                                      { .name = "longitude", .value = "6.864722" },
                                      { .name = NULL, .value = NULL } };
    if (MENDER_OK != mender_inventory_set(inventory)) {
        ESP_LOGE(TAG, "Unable to set mender inventory");
    }
#endif /* CONFIG_MENDER_CLIENT_ADD_ON_INVENTORY */

    /* Finally activate mender client */
    if (MENDER_OK != mender_client_activate()) {
        ESP_LOGE(TAG, "Unable to activate mender-client");
        goto RELEASE;
    }

    /* Wait for mender-mcu-client events, connect and disconnect network on request, restart the application if required */
    bool connected = false;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            mender_client_events, MENDER_CLIENT_EVENT_CONNECT | MENDER_CLIENT_EVENT_DISCONNECT | MENDER_CLIENT_EVENT_RESTART, pdTRUE, pdFALSE, portMAX_DELAY);
        if (MENDER_CLIENT_EVENT_CONNECT == (bits & MENDER_CLIENT_EVENT_CONNECT)) {
            /* Connect to the network */
            ESP_LOGI(TAG, "Connecting to the network");
            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_CONNECTING);
            if (ESP_OK != example_connect()) {
                xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_DISCONNECTED);
                ESP_LOGE(TAG, "Unable to connect network");
            } else {
                connected = true;
                xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECTED);
                xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_CONNECTED);
                ESP_LOGI(TAG, "Connected to the network");
            }
        } else if (MENDER_CLIENT_EVENT_DISCONNECT == (bits & MENDER_CLIENT_EVENT_DISCONNECT)) {
            bits = xEventGroupWaitBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECT, pdTRUE, pdFALSE, 10000 / portTICK_PERIOD_MS);
            if (MENDER_CLIENT_EVENT_CONNECT == (bits & MENDER_CLIENT_EVENT_CONNECT)) {
                /* Reconnection requested while not disconnected yet */
                xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECTED);
                ESP_LOGI(TAG, "Connected to the network");
            } else {
                /* Disconnect the network */
                ESP_LOGI(TAG, "Disconnecting network");
                xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_DISCONNECTING);
                if (ESP_OK != example_disconnect()) {
                    ESP_LOGE(TAG, "Unable to disconnect network");
                } else {
                    connected = false;
                    xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_DISCONNECTED);
                    ESP_LOGI(TAG, "Disconnected of the network");
                }
            }
        }
        if (MENDER_CLIENT_EVENT_RESTART == (bits & MENDER_CLIENT_EVENT_RESTART)) {
            while (1) {
                bits = xEventGroupWaitBits(
                    mender_client_events, MENDER_CLIENT_EVENT_CONNECT | MENDER_CLIENT_EVENT_DISCONNECT, pdTRUE, pdFALSE, 10000 / portTICK_PERIOD_MS);
                if (MENDER_CLIENT_EVENT_CONNECT == (bits & MENDER_CLIENT_EVENT_CONNECT)) {
                    /* Reconnection requested before restarting */
                    if (!connected) {
                        /* Connect to the network */
                        ESP_LOGI(TAG, "Connecting to the network");
                        xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_CONNECTING);
                        if (ESP_OK != example_connect()) {
                            xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_DISCONNECTED);
                            ESP_LOGE(TAG, "Unable to connect network");
                        } else {
                            connected = true;
                            xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECTED);
                            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_CONNECTED);
                            ESP_LOGI(TAG, "Connected to the network");
                        }
                    } else {
                        xEventGroupSetBits(mender_client_events, MENDER_CLIENT_EVENT_CONNECTED);
                        ESP_LOGI(TAG, "Connected to the network");
                    }
                } else if (MENDER_CLIENT_EVENT_DISCONNECT == (bits & MENDER_CLIENT_EVENT_DISCONNECT)) {
                    /* Disonnection requested before restarting */
                    if (connected) {
                        /* Disconnect the network */
                        ESP_LOGI(TAG, "Disconnecting network");
                        xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_DISCONNECTING);
                        if (ESP_OK != example_disconnect()) {
                            ESP_LOGE(TAG, "Unable to disconnect network");
                        } else {
                            connected = false;
                            xEventGroupSetBits(mender_shield_events, MENDER_SHIELD_EVENT_DISCONNECTED);
                            ESP_LOGI(TAG, "Disconnected of the network");
                        }
                    }
                } else {
                    /* Application will restart now */
                    goto RELEASE;
                }
            }
        }
    }

RELEASE:

    /* Deactivate and release mender-client */
    mender_client_deactivate();
    mender_client_exit();

    /* Release event groups */
    vEventGroupDelete(mender_client_events);
    vEventGroupDelete(mender_shield_events);

    /* Release mender-shield hardware */
    shield_exit();

    /* Restart */
    ESP_LOGI(TAG, "Restarting system");
    esp_restart();
}
