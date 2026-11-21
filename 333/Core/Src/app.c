#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "OLED.h"

#define KEY1_PIN            GPIO_PIN_13
#define KEY2_PIN            GPIO_PIN_14
#define KEY_DEBOUNCE_MS     30

#define STEP_MIN            100u
#define STEP_MAX            2000u

typedef struct {
    uint8_t  run;
    uint8_t  dir;
    uint16_t step;
    uint8_t  led;
} LightState;

static volatile LightState st = {1, 0, 300, 0};

static volatile uint8_t  key1_evt = 0, key2_evt = 0;
static volatile uint32_t key1_last = 0, key2_last = 0;

static uint8_t  rx_byte = 0;
static uint8_t  rx_buf[6] = {0};
static uint8_t  rx_idx = 0;
static volatile uint8_t pkt_ready = 0;

static UART_HandleTypeDef huart1;

static volatile uint8_t tx_busy = 0;
static volatile uint8_t tx_pending = 0;
static char tx_msg[64];

static uint8_t  dirty = 1;
static uint32_t last_tick = 0;

static void     led_set(uint8_t idx);
static void     oled_render(void);
static void     feedback_request(void);
static void     feedback_service(void);
static uint8_t  packet_checksum_ok(const uint8_t *p);
static void     exti_init(void);
static void     uart_init(void);

static void led_set(uint8_t idx)
{
    HAL_GPIO_WritePin(LED0_GPIO_Port,
                      LED0_Pin | LED1_Pin | LED2_Pin,
                      GPIO_PIN_RESET);

    switch (idx) {
    case 0:  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET); break;
    case 1:  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET); break;
    default: HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET); break;
    }
}

static void oled_render(void)
{
    OLED_ShowString(1, 1,  "STATE: ");
    OLED_ShowString(1, 8,  st.run ? "RUN  " : "PAUSE");
    OLED_ShowString(2, 1,  "DIR:   ");
    OLED_ShowString(2, 8,  st.dir ? "REV " : "FWD ");
    OLED_ShowString(3, 1,  "STEP:  ");
    OLED_ShowNum(3, 8, st.step, 4);
    OLED_ShowString(3, 12, "ms");
    OLED_ShowString(4, 1,  "LED:   ");
    OLED_ShowNum(4, 8, st.led + 1, 1);
}

static void feedback_request(void)
{
    tx_pending = 1;
}

static void feedback_service(void)
{
    int n;

    if (!tx_pending || tx_busy) return;
    tx_pending = 0;

    n = snprintf(tx_msg, sizeof(tx_msg),
                 "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
                 st.run ? "RUN" : "PAUSE",
                 st.dir ? "REV" : "FWD",
                 (unsigned)st.step,
                 (unsigned)(st.led + 1));
    if (n <= 0 || n >= (int)sizeof(tx_msg)) return;

    tx_busy = 1;
    if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)tx_msg, (uint16_t)n) != HAL_OK) {
        tx_busy = 0;
        tx_pending = 1;
    }
}

static uint8_t packet_checksum_ok(const uint8_t *p)
{
    uint8_t sum = 0;
    uint8_t i;

    for (i = 0; i < 5; i++) {
        sum += p[i];
    }
    return (sum == p[5]);
}

static void exti_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin  = KEY1_PIN | KEY2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (uartHandle->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin   = GPIO_PIN_9;
        GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin  = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

static void uart_init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin == KEY1_PIN && (now - key1_last) > KEY_DEBOUNCE_MS) {
        key1_last = now;
        key1_evt = 1;
    }
    if (GPIO_Pin == KEY2_PIN && (now - key2_last) > KEY_DEBOUNCE_MS) {
        key2_last = now;
        key2_evt = 1;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        if (rx_idx == 0) {
            if (rx_byte == 0xAA) {
                rx_buf[rx_idx++] = rx_byte;
            }
        } else {
            rx_buf[rx_idx++] = rx_byte;
            if (rx_idx >= 6) {
                rx_idx = 0;
                pkt_ready = 1;
            }
        }
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_busy = 0;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        rx_idx = 0;
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(KEY1_PIN);
    HAL_GPIO_EXTI_IRQHandler(KEY2_PIN);
}

void App_Init(void)
{
    OLED_Init();
    led_set(st.led);
    oled_render();

    uart_init();
    exti_init();

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

    last_tick = HAL_GetTick();
    dirty = 1;
}

void App_Loop(void)
{
    if (key1_evt) {
        key1_evt = 0;
        st.dir ^= 1;
        dirty = 1;
        feedback_request();
    }
    if (key2_evt) {
        key2_evt = 0;
        st.run ^= 1;
        if (st.run) last_tick = HAL_GetTick();
        dirty = 1;
        feedback_request();
    }

    if (pkt_ready) {
        uint8_t buf[6];

        __disable_irq();
        memcpy(buf, rx_buf, 6);
        pkt_ready = 0;
        __enable_irq();

        if (buf[0] == 0xAA && packet_checksum_ok(buf)) {
            uint16_t s = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);

            if (s < STEP_MIN) s = STEP_MIN;
            if (s > STEP_MAX) s = STEP_MAX;

            st.run  = (buf[1] == 0x01);
            st.dir  = (buf[2] == 0x01);
            st.step = s;
            last_tick = HAL_GetTick();
            dirty = 1;
            feedback_request();
        }
    }

    if (st.run && (uint32_t)(HAL_GetTick() - last_tick) >= st.step) {
        last_tick = HAL_GetTick();
        st.led = st.dir ? (uint8_t)((st.led + 2) % 3)
                        : (uint8_t)((st.led + 1) % 3);
        led_set(st.led);
        dirty = 1;
    }

    if (dirty) {
        dirty = 0;
        oled_render();
    }

    feedback_service();
}
