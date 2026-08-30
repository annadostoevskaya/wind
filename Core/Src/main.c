/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_logic.h"
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
  APP_STATE_STARTUP = 0,
  APP_STATE_RAK_OFF_WAIT,
  APP_STATE_RAK_BOOT,
  APP_STATE_JOINING,
  APP_STATE_ACTIVE_POWER_SETTLE,
  APP_STATE_ACTIVE,
  APP_STATE_STOP,
  APP_STATE_WAKE_BATTERY,
  APP_STATE_WAKE_ROTATION_SETTLE,
  APP_STATE_WAKE_ROTATION_CHECK,
  APP_STATE_EMERGENCY
} AppState;

typedef enum
{
  JOIN_STATE_IDLE = 0,
  JOIN_STATE_WAIT_ACCEPT,
  JOIN_STATE_WAIT_RESULT,
  JOIN_STATE_WAIT_STATUS,
  JOIN_STATE_BUSY_BACKOFF,
  JOIN_STATE_RETRY_WAIT,
  JOIN_STATE_BATTERY_CHECK
} JoinState;

typedef enum
{
  UPLINK_STATE_IDLE = 0,
  UPLINK_STATE_WAIT_ACCEPT,
  UPLINK_STATE_WAIT_RESULT,
  UPLINK_STATE_RETRY_WAIT
} UplinkState;

typedef enum
{
  BATTERY_REASON_NONE = 0,
  BATTERY_REASON_JOIN_FAILURES,
  BATTERY_REASON_JOIN_PERIODIC,
  BATTERY_REASON_ACTIVE_PERIODIC,
  BATTERY_REASON_ROTATION_STOP,
  BATTERY_REASON_PWR_INPUT,
  BATTERY_REASON_WAKEUP,
  BATTERY_REASON_WAKE_ROTATION_RESULT
} BatteryReason;

typedef enum
{
  DS18B20_STATE_IDLE = 0,
  DS18B20_STATE_CONVERTING
} DS18B20_State;

typedef struct
{
  bool valid;
  uint16_t raw;
  float voltage;
} BatteryResult;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_CHANNELS            6U
#define ADC_MAX                 4095.0f
#define VREF                    3.0f
#define ANALOG_MIDPOINT_V       1.5f
#define RMS_SAMPLES             5000U

#define ADC_DMA_HALF_SEQUENCES  500U
#define ADC_DMA_HALF_VALUES     (ADC_CHANNELS * ADC_DMA_HALF_SEQUENCES)
#define ADC_DMA_BUFFER_VALUES   (ADC_DMA_HALF_VALUES * 2U)

#define ADC_IDX_VA              0U
#define ADC_IDX_VB              1U
#define ADC_IDX_VC              2U
#define ADC_IDX_IA              3U
#define ADC_IDX_IB              4U
#define ADC_IDX_IC              5U

#define CURRENT_RSHUNT_OHM      0.016f
#define CURRENT_GAIN_STAGE1     (100000.0f / 8200.0f)
#define CURRENT_GAIN_TOTAL      (CURRENT_GAIN_STAGE1 / 2.0f)
#define CURRENT_V_PER_AMP       (CURRENT_RSHUNT_OHM * CURRENT_GAIN_TOTAL)

#define VOLTAGE_R_TOP_OHM       100000.0f
#define VOLTAGE_R_BOTTOM_OHM    10000.0f
#define VOLTAGE_DIV_RATIO       (VOLTAGE_R_BOTTOM_OHM / (VOLTAGE_R_TOP_OHM + VOLTAGE_R_BOTTOM_OHM))
#define VOLTAGE_GAIN_TOTAL      (VOLTAGE_DIV_RATIO / 2.0f)

#define BAT_DIV_R_TOP           10000.0f
#define BAT_DIV_R_BOTTOM        13000.0f
#define BAT_VOLTAGE_CORRECTION  1.10f
#define BAT_LOW_THRESHOLD_V     3.6f

#define PULSES_PER_REV          6U

#define MEASUREMENT_POWER_SETTLE_MS  100U
#define ROTATION_REPORT_WINDOW_MS    2333U
#define ROTATION_CHECK_WINDOW_MS     10000U /* ON_FR remains on for this window. */

#define BATTERY_SETTLE_MS            50U
#define BATTERY_AVERAGE_SAMPLES       8U
#define BATTERY_ADC_RETRIES           3U
#define BATTERY_ACTIVE_PERIOD_MS      30000U
#define BATTERY_ERROR_RETRY_MS        5000U

#define RAK_UART                huart1
#define RAK_RX_RING_SIZE        512U
#define RAK_RX_BUFFER_SIZE      192U
#define RAK_PAYLOAD_TEXT_SIZE   160U
#define RAK_PAYLOAD_HEX_SIZE    ((RAK_PAYLOAD_TEXT_SIZE * 2U) + 1U)
#define RAK_SEND_CMD_SIZE       (24U + RAK_PAYLOAD_HEX_SIZE)
#define RAK_MEAS_PAYLOAD_SIZE   33U
#define RAK_JOIN_ACCEPT_TIMEOUT_MS 5000U
#define RAK_JOIN_RESULT_TIMEOUT_MS 60000U
#define RAK_JOIN_STATUS_TIMEOUT_MS 5000U
#define RAK_JOIN_RETRY_DELAY_MS    10000U
#define RAK_BUSY_BACKOFF_MS        3000U
#define RAK_JOIN_FAILURES_PER_BATTERY_CHECK 10U
#define RAK_AT_TIMEOUT_MS       5000U
#define RAK_BOOT_DELAY_MS       5000U
#define RAK_POWER_OFF_DELAY_MS  1000U
#define RAK_UART_TX_TIMEOUT_MS  25U

/* Product requirement: request a telemetry uplink every five seconds.
   The RAK modem may still defer/reject a request to enforce regional
   duty-cycle limits. */
#ifndef UPLINK_PERIOD_MS
#define UPLINK_PERIOD_MS        5000U
#endif
#define UPLINK_RESULT_TIMEOUT_MS 30000U
#define UPLINK_BUSY_RETRY_MS    5000U

#define DEBUG_UART              huart2
#define DEBUG_LOG_BUFFER_SIZE   192U
#define DEBUG_UART_TX_TIMEOUT_MS 20U

#define DS18B20_MAX_SENSORS     4U
#define DS18B20_SCRATCHPAD_SIZE 9U
#define DS18B20_CONVERT_MS      750U
#define DS18B20_PERIOD_MS       30000U
#define DS18B20_EMPTY_RETRY_MS  5000U
#define DS18B20_PARTIAL_RETRY_MS 60000U

#define STOP_WAKEUP_SECONDS     10U
#define RTC_ASYNC_PREDIV        127U
#define RTC_SYNC_PREDIV         249U

#define STATUS_ADC_WINDOW_ERROR (1U << 0)
#define STATUS_ADC_SATURATION   (1U << 1)
#define STATUS_AUX_ADC_ERROR    (1U << 2)
#define STATUS_DS18B20_ERROR    (1U << 3)
#define STATUS_JOIN_LOST        (1U << 4)
#define STATUS_SEND_ERROR       (1U << 5)
#define STATUS_UART_OVERFLOW    (1U << 6)
#define STATUS_BATTERY_INVALID  (1U << 7)

_Static_assert((RAK_RX_RING_SIZE & (RAK_RX_RING_SIZE - 1U)) == 0U,
               "RAK_RX_RING_SIZE must be a power of two");
_Static_assert(ADC_DMA_BUFFER_VALUES == 6000U,
               "ADC DMA buffer must contain two 500-sequence halves");

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
_Alignas(uint32_t) static uint16_t adc_buffer[ADC_DMA_BUFFER_VALUES] = {0};
static float sum[ADC_CHANNELS] = {0};
static float sum_sq[ADC_CHANNELS] = {0};
static float rms_voltage[3] = {0};
static float rms_current[3] = {0};
static uint32_t rms_count = 0;
static bool rms_valid = false;
static volatile uint8_t adc_dma_pending_mask = 0;
static volatile uint8_t adc_dma_overrun = 0;
static volatile uint8_t adc_dma_error = 0;
static volatile uint32_t adc_dma_generation = 0;
static bool measurements_running = false;
static bool measurement_clocks_enabled = true;

static volatile uint32_t frequency_pulse_count = 0;
static volatile uint32_t rotation_check_pulse_count = 0;
static volatile bool rotation_count_enabled = false;
static float revolutions = 0.0f;
static uint32_t rotation_report_tick = 0;
static uint32_t rotation_check_tick = 0;

static uint16_t adc_x_raw = 0;
static uint16_t adc_y_raw = 0;
static uint16_t adc_z_raw = 0;
static uint16_t adc_bat_raw = 0;
static float battery_voltage = 0.0f;

static uint8_t ds18b20_roms[DS18B20_MAX_SENSORS][8] = {0};
static uint8_t ds18b20_count = 0;
static int32_t ds18b20_temp_centi[DS18B20_MAX_SENSORS] = {0};
static bool ds18b20_temp_ok[DS18B20_MAX_SENSORS] = {0};
static DS18B20_State ds18b20_state = DS18B20_STATE_IDLE;
static uint32_t ds18b20_deadline = 0;
static uint32_t ds18b20_next_tick = 0;
static uint32_t ds18b20_discovery_tick = 0;

static uint8_t rak_rx_byte = 0;
static uint8_t rak_rx_ring[RAK_RX_RING_SIZE] = {0};
static volatile uint16_t rak_rx_head = 0;
static volatile uint16_t rak_rx_tail = 0;
static volatile uint8_t rak_rx_overflow = 0;
static volatile uint8_t rak_rx_error = 0;
static volatile uint8_t rak_rx_fault_pending = 0;
static volatile uint16_t rak_rx_fault_boundary = 0;
static char rak_rx_buffer[RAK_RX_BUFFER_SIZE] = {0};
static uint16_t rak_rx_line_length = 0;
static bool rak_rx_discard_line = false;
static bool rak_uart_rx_enabled = false;
static bool rak_uart_initialized = true;
static uint32_t rak_events = 0;
static bool debug_uart_ready = false;

static AppState app_state = APP_STATE_STARTUP;
static JoinState join_state = JOIN_STATE_IDLE;
static UplinkState uplink_state = UPLINK_STATE_IDLE;
static uint32_t app_deadline = 0;
static uint32_t join_deadline = 0;
static uint32_t join_attempt_number = 0;
static uint32_t join_failed_attempts = 0;
static uint8_t join_failures_since_battery = 0;
static bool join_confirmed_pending = false;
static bool join_status_after_busy = false;
static uint32_t uplink_deadline = 0;
static uint32_t next_uplink_tick = 0;
static uint8_t consecutive_send_failures = 0;

static BatteryReason battery_request = BATTERY_REASON_NONE;
static BatteryReason battery_completed_reason = BATTERY_REASON_NONE;
static uint32_t battery_deadline = 0;
static BatteryResult battery_completed_result = {false, 0U, 0.0f};
static bool battery_result_ready = false;
static uint32_t next_battery_tick = 0;
static uint32_t next_join_battery_tick = 0;
static bool pwr_input_low_latched = false;
static BatteryReason wake_battery_reason = BATTERY_REASON_WAKEUP;
static uint32_t wake_rotation_pulses = 0;

static uint8_t device_status_flags = 0;
static bool rtc_wakeup_ready = false;
static volatile uint8_t rtc_wakeup_fired = 0;
static uint32_t led_off_deadline = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
static void DEBUG_USART2_GPIO_Clock_Init(void);
static void Debug_Log(const char *fmt, ...);
static void App_AssertPowerHoldEarly(void);
static void App_Init(void);
static void App_Run(void);
static void App_SetState(AppState new_state, const char *reason);
static const char *App_StateName(AppState state);
static bool TimeReached(uint32_t now, uint32_t deadline);
static void App_SetExternalPower(bool rak, bool analog, bool frequency, bool auxiliary, bool power_detect);
static void App_StartRakBoot(uint32_t now);
static void App_StartActiveAfterJoin(uint32_t now, const char *confirmation);
static void App_RequestReconnect(uint32_t now, const char *reason);
static void App_EnterEmergency(const char *reason);
static void App_EnterStopCycle(void);

static void LED_Indicate(bool success, uint32_t now);
static void LED_Service(uint32_t now);
static void DWT_Delay_Init(void);
static void DWT_DelayUs(uint32_t us);
static uint32_t App_GetTim2ClockHz(void);
static void App_LogSamplingConfiguration(void);

static bool App_StartMeasurements(uint32_t now);
static bool App_RestartAdcAcquisition(void);
static void App_StopMeasurements(void);
static void App_EnableMeasurementClocks(void);
static void App_DisableMeasurementClocks(void);
static void App_ProcessAdcDma(void);
static void App_ProcessAdcBlock(uint16_t offset);
static bool App_AdcGenerationStable(uint32_t generation);
static void App_ResetRmsAccumulator(void);
static void App_CalculateRmsWindow(void);
static void App_ServiceRotation(uint32_t now);
static uint32_t Rotation_TakeReportPulses(void);
static uint32_t Rotation_TakeCheckPulses(void);
static void Rotation_ResetCounters(void);
static void Rotation_SetExtiEnabled(bool enable);

static bool ADC2_ReadChannel(uint32_t channel, uint16_t *value);
static bool ReadAuxAnalogInputs(void);
static BatteryResult Battery_ReadReliable(void);
static void Battery_Start(BatteryReason reason, uint32_t now);
static void Battery_Service(uint32_t now);
static void Battery_HandleCompleted(uint32_t now);
static float RawToVoltage(uint16_t raw);
static float BatteryVoltageFromRaw(uint16_t raw);

static void TextAppend(char *dst, uint16_t dst_size, uint16_t *pos, const char *src);
static void TextAppendInt32(char *dst, uint16_t dst_size, uint16_t *pos, int32_t value);

static bool RAK_UartStart(void);
static void RAK_UartStop(void);
static void RAK_ResetParser(void);
static void RAK_ProcessRx(void);
static uint32_t RAK_ClassifyLine(const char *line);
static bool RAK_SendCommand(const char *cmd);
static bool RAK_SendJoinCommand(void);
static bool RAK_SendNetworkStatusCommand(void);
static void RAK_StartJoinAttempt(uint32_t now);
static void RAK_RecordJoinFailure(uint32_t now, const char *reason);
static void RAK_HandleJoin(uint32_t now, uint32_t events);
static void RAK_HandleUplink(uint32_t now, uint32_t events);
static void RAK_CompleteUplinkFailure(uint32_t now, const char *reason);
static void RAK_BytesToHex(const uint8_t *bytes, uint16_t length, char *hex, uint16_t hex_size);
static bool RAK_SendBytesPayload(uint8_t port, const uint8_t *payload, uint16_t payload_size);
static bool BuildMeasurementPayload(uint8_t *payload, uint16_t payload_size);

static void DS18B20_LineConfigureActive(void);
static void DS18B20_LineSafeOff(void);
static void DS18B20_LineRelease(void);
static void DS18B20_LineLow(void);
static uint8_t DS18B20_LineRead(void);
static uint8_t DS18B20_Reset(void);
static void DS18B20_WriteBit(uint8_t bit);
static uint8_t DS18B20_ReadBit(void);
static void DS18B20_WriteByte(uint8_t byte);
static uint8_t DS18B20_ReadByte(void);
static uint8_t DS18B20_Crc8(const uint8_t *data, uint8_t len);
static uint8_t DS18B20_Search(uint8_t roms[][8], uint8_t max_devices);
static bool DS18B20_ReadScratchpad(const uint8_t rom[8], uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE]);
static bool DS18B20_ReadTemperatureCenti(const uint8_t rom[8], int32_t *temp_centi);
static void DS18B20_DiscoverCached(uint32_t now);
static bool DS18B20_StartConversion(void);
static void DS18B20_Service(uint32_t now);

static bool RTC_WakeupInit(void);
static bool RTC_WakeupStart(uint32_t seconds);
static void RTC_WakeupStop(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void DEBUG_USART2_GPIO_Clock_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

static void Debug_Log(const char *fmt, ...)
{
  char buffer[DEBUG_LOG_BUFFER_SIZE];
  va_list args;
  int len;

  if (debug_uart_ready == false)
  {
    return;
  }

  va_start(args, fmt);
  len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len <= 0)
  {
    return;
  }

  if (len >= (int)sizeof(buffer))
  {
    len = (int)sizeof(buffer) - 1;
  }

  (void)HAL_UART_Transmit(&DEBUG_UART, (uint8_t *)buffer, (uint16_t)len,
                          DEBUG_UART_TX_TIMEOUT_MS);
}

static void App_AssertPowerHoldEarly(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = PWR_OFF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PWR_OFF_GPIO_Port, &GPIO_InitStruct);
}

static bool TimeReached(uint32_t now, uint32_t deadline)
{
  return AppLogic_DeadlineReached(now, deadline);
}

static const char *App_StateName(AppState state)
{
  switch (state)
  {
    case APP_STATE_STARTUP: return "STARTUP";
    case APP_STATE_RAK_OFF_WAIT: return "RAK_OFF_WAIT";
    case APP_STATE_RAK_BOOT: return "RAK_BOOT";
    case APP_STATE_JOINING: return "JOINING";
    case APP_STATE_ACTIVE_POWER_SETTLE: return "ACTIVE_POWER_SETTLE";
    case APP_STATE_ACTIVE: return "ACTIVE";
    case APP_STATE_STOP: return "STOP";
    case APP_STATE_WAKE_BATTERY: return "WAKE_BATTERY";
    case APP_STATE_WAKE_ROTATION_SETTLE: return "WAKE_ROTATION_SETTLE";
    case APP_STATE_WAKE_ROTATION_CHECK: return "WAKE_ROTATION_CHECK";
    case APP_STATE_EMERGENCY: return "EMERGENCY";
    default: return "UNKNOWN";
  }
}

static void App_SetState(AppState new_state, const char *reason)
{
  if (app_state != new_state)
  {
    Debug_Log("[FSM] %s -> %s: %s\r\n",
              App_StateName(app_state), App_StateName(new_state), reason);
    app_state = new_state;
  }
}

static void App_SetExternalPower(bool rak, bool analog, bool frequency,
                                 bool auxiliary, bool power_detect)
{
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ON_RAK_GPIO_Port, ON_RAK_Pin, rak ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_AN_GPIO_Port, ON_AN_Pin, analog ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_FR_GPIO_Port, ON_FR_Pin, frequency ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_AX_GPIO_Port, ON_AX_Pin, auxiliary ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_PWR_DET_GPIO_Port, ON_PWR_DET_Pin,
                    power_detect ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void LED_Indicate(bool success, uint32_t now)
{
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10 | GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, success ? GPIO_PIN_10 : GPIO_PIN_14, GPIO_PIN_SET);
  led_off_deadline = now + 200U;
}

static void LED_Service(uint32_t now)
{
  if ((led_off_deadline != 0U) && TimeReached(now, led_off_deadline))
  {
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10 | GPIO_PIN_14, GPIO_PIN_RESET);
    led_off_deadline = 0U;
  }
}

static void DWT_Delay_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DWT_DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000U);

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

static uint32_t App_GetTim2ClockHz(void)
{
  uint32_t timer_clock_hz = HAL_RCC_GetPCLK1Freq();

  /* On STM32F4, an APB prescaler other than 1 doubles the timer clock. */
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    timer_clock_hz *= 2U;
  }
  return timer_clock_hz;
}

static void App_LogSamplingConfiguration(void)
{
  uint32_t timer_clock_hz = App_GetTim2ClockHz();
  uint32_t prescaler = htim2.Instance->PSC;
  uint32_t autoreload = htim2.Instance->ARR;
  uint64_t divider = ((uint64_t)prescaler + 1ULL) *
                     ((uint64_t)autoreload + 1ULL);
  uint32_t sample_rate_millihz =
    (uint32_t)((((uint64_t)timer_clock_hz * 1000ULL) + (divider / 2ULL)) /
               divider);
  uint32_t samples_per_50hz_x100 =
    (sample_rate_millihz + 250U) / 500U;
  uint32_t rms_window_us =
    (uint32_t)((((uint64_t)RMS_SAMPLES * 1000000000ULL) +
                ((uint64_t)sample_rate_millihz / 2ULL)) /
               sample_rate_millihz);

  Debug_Log("[MEAS] TIM2=%lu Hz PSC=%lu ARR=%lu Fs=%lu.%03lu Hz, 50Hz samples=%lu.%02lu, RMS window=%lu.%03lu ms\r\n",
            timer_clock_hz, prescaler, autoreload,
            sample_rate_millihz / 1000U, sample_rate_millihz % 1000U,
            samples_per_50hz_x100 / 100U,
            samples_per_50hz_x100 % 100U,
            rms_window_us / 1000U, rms_window_us % 1000U);
}

static uint32_t Critical_Enter(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void Critical_Exit(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static bool RTC_WakeupInit(void)
{
  uint32_t start = HAL_GetTick();

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSI_ENABLE();

  while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET)
  {
    if ((HAL_GetTick() - start) > 100U)
    {
      return false;
    }
  }

  if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_RTCCLKSOURCE_LSI)
  {
    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
  }

  __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
  __HAL_RCC_RTC_ENABLE();

  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->ISR |= RTC_ISR_INIT;
  start = HAL_GetTick();
  while ((RTC->ISR & RTC_ISR_INITF) == 0U)
  {
    if ((HAL_GetTick() - start) > 100U)
    {
      RTC->WPR = 0xFFU;
      return false;
    }
  }
  /* 127/249 yields a nominal 1 Hz ck_spre when LSI is at its nominal
     32 kHz.  LSI is not calibrated in this project and has device/temperature
     tolerance, so STOP_WAKEUP_SECONDS is a nominal, not exact, interval. */
  RTC->PRER = (RTC_ASYNC_PREDIV << RTC_PRER_PREDIV_A_Pos) | RTC_SYNC_PREDIV;
  RTC->ISR &= ~RTC_ISR_INIT;
  RTC->WPR = 0xFFU;

  EXTI->IMR |= EXTI_IMR_MR22;
  EXTI->RTSR |= EXTI_RTSR_TR22;
  EXTI->FTSR &= ~EXTI_FTSR_TR22;
  EXTI->PR = EXTI_PR_PR22;
  HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

  return true;
}

static bool RTC_WakeupStart(uint32_t seconds)
{
  uint32_t guard = SystemCoreClock / 1000U;
  uint32_t reload;

  if (!AppLogic_RtcWakeReload(seconds, &reload))
  {
    return false;
  }

  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  while (((RTC->ISR & RTC_ISR_WUTWF) == 0U) && (guard > 0U))
  {
    guard--;
  }
  if (guard == 0U)
  {
    RTC->WPR = 0xFFU;
    return false;
  }

  RTC->ISR &= ~RTC_ISR_WUTF;
  RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | RTC_CR_WUCKSEL_2;
  RTC->WUTR = reload;
  EXTI->PR = EXTI_PR_PR22;
  HAL_NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
  rtc_wakeup_fired = 0U;
  RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
  RTC->WPR = 0xFFU;
  __DSB();

  return true;
}

static void RTC_WakeupStop(void)
{
  uint32_t guard = SystemCoreClock / 1000U;

  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  while (((RTC->ISR & RTC_ISR_WUTWF) == 0U) && (guard > 0U))
  {
    guard--;
  }
  RTC->ISR &= ~RTC_ISR_WUTF;
  RTC->WPR = 0xFFU;
  EXTI->PR = EXTI_PR_PR22;
  HAL_NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
  __DSB();
}

void App_RTCWakeupIRQ(void)
{
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->ISR &= ~RTC_ISR_WUTF;
  RTC->WPR = 0xFFU;
  EXTI->PR = EXTI_PR_PR22;
  rtc_wakeup_fired = 1U;
  __DSB();
}

static bool ADC2_ReadChannel(uint32_t channel, uint16_t *value)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  if (value == NULL)
  {
    return false;
  }

  __HAL_RCC_ADC2_CLK_ENABLE();

  HAL_ADC_Stop(&hadc2);

  sConfig.Channel = channel;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;

  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Debug_Log("[ADC2] config failed channel=%lu\r\n", channel);
    return false;
  }

  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
    Debug_Log("[ADC2] start failed channel=%lu\r\n", channel);
    return false;
  }

  if (HAL_ADC_PollForConversion(&hadc2, 5U) != HAL_OK)
  {
    Debug_Log("[ADC2] poll timeout channel=%lu\r\n", channel);
    HAL_ADC_Stop(&hadc2);
    return false;
  }

  *value = (uint16_t)HAL_ADC_GetValue(&hadc2);
  HAL_ADC_Stop(&hadc2);

  return true;
}

static BatteryResult Battery_ReadReliable(void)
{
  BatteryResult result = {false, 0U, 0.0f};

  for (uint8_t attempt = 0; attempt < BATTERY_ADC_RETRIES; attempt++)
  {
    AppLogicBatteryAccumulator accumulator;
    uint16_t discard = 0;
    bool ok = ADC2_ReadChannel(ADC_CHANNEL_3, &discard);

    AppLogic_BatteryAccumulatorInit(&accumulator);

    for (uint8_t i = 0; (i < BATTERY_AVERAGE_SAMPLES) && ok; i++)
    {
      uint16_t sample = 0;
      ok = ADC2_ReadChannel(ADC_CHANNEL_3, &sample);
      if (!AppLogic_BatteryAccumulatorAdd(&accumulator, ok, sample,
                                          (uint16_t)ADC_MAX))
      {
        ok = false;
      }
    }

    if (accumulator.at_rail)
    {
      device_status_flags |= STATUS_ADC_SATURATION;
    }

    if (ok && AppLogic_BatteryAccumulatorAverage(
                &accumulator, BATTERY_AVERAGE_SAMPLES, &result.raw))
    {
      result.voltage = BatteryVoltageFromRaw(result.raw);
      result.valid = true;
      return result;
    }

    Debug_Log("[BAT] ADC retry %u/%u failed\r\n",
              (unsigned int)(attempt + 1U), (unsigned int)BATTERY_ADC_RETRIES);
  }

  return result;
}

static bool ReadAuxAnalogInputs(void)
{
  uint16_t value = 0;
  bool ok = true;
  BatteryResult battery;

  if (ADC2_ReadChannel(ADC_CHANNEL_0, &value))
  {
    adc_z_raw = value;
    if (AppLogic_AdcRawAtRail(value, (uint16_t)ADC_MAX))
    {
      device_status_flags |= STATUS_ADC_SATURATION;
    }
  }
  else
  {
    ok = false;
  }

  if (ADC2_ReadChannel(ADC_CHANNEL_1, &value))
  {
    adc_y_raw = value;
    if (AppLogic_AdcRawAtRail(value, (uint16_t)ADC_MAX))
    {
      device_status_flags |= STATUS_ADC_SATURATION;
    }
  }
  else
  {
    ok = false;
  }

  if (ADC2_ReadChannel(ADC_CHANNEL_2, &value))
  {
    adc_x_raw = value;
    if (AppLogic_AdcRawAtRail(value, (uint16_t)ADC_MAX))
    {
      device_status_flags |= STATUS_ADC_SATURATION;
    }
  }
  else
  {
    ok = false;
  }

  battery = Battery_ReadReliable();
  if (battery.valid)
  {
    adc_bat_raw = battery.raw;
    battery_voltage = battery.voltage;
    device_status_flags &= (uint8_t)~STATUS_BATTERY_INVALID;
  }
  else
  {
    ok = false;
    device_status_flags |= STATUS_BATTERY_INVALID;
  }

  if (ok)
  {
    device_status_flags &= (uint8_t)~STATUS_AUX_ADC_ERROR;
  }
  else
  {
    device_status_flags |= STATUS_AUX_ADC_ERROR;
  }

  Debug_Log("[ADC2] aux raw: X=%u Y=%u Z=%u BAT=%u BAT_x100=%ld\r\n",
            adc_x_raw, adc_y_raw, adc_z_raw, adc_bat_raw,
            (int32_t)(battery_voltage * 100.0f));

  return ok;
}

static float RawToVoltage(uint16_t raw)
{
  return ((float)raw * VREF) / ADC_MAX;
}

static float BatteryVoltageFromRaw(uint16_t raw)
{
  return RawToVoltage(raw) *
         ((BAT_DIV_R_TOP + BAT_DIV_R_BOTTOM) / BAT_DIV_R_BOTTOM) *
         BAT_VOLTAGE_CORRECTION;
}

static void App_ResetRmsAccumulator(void)
{
  memset(sum, 0, sizeof(sum));
  memset(sum_sq, 0, sizeof(sum_sq));
  rms_count = 0U;
}

static void Rotation_ResetCounters(void)
{
  uint32_t primask = Critical_Enter();
  frequency_pulse_count = 0U;
  rotation_check_pulse_count = 0U;
  Critical_Exit(primask);
}

static void Rotation_SetExtiEnabled(bool enable)
{
  uint32_t primask = Critical_Enter();

  if (enable)
  {
    /* Clear only EXTI12 before exposing it to the shared EXTI15_10 IRQ. */
    EXTI->PR = (uint32_t)Freqency_Pin;
    rotation_count_enabled = true;
    EXTI->IMR = AppLogic_ExtiMaskUpdate(EXTI->IMR,
                                        (uint32_t)Freqency_Pin, true);
  }
  else
  {
    /* EXTI15_10 is shared: mask only the frequency line and leave the
       common NVIC IRQ available for any other EXTI10..15 source. */
    EXTI->IMR = AppLogic_ExtiMaskUpdate(EXTI->IMR,
                                        (uint32_t)Freqency_Pin, false);
    rotation_count_enabled = false;
    EXTI->PR = (uint32_t)Freqency_Pin;
  }
  __DSB();
  Critical_Exit(primask);
}

static uint32_t Rotation_TakeReportPulses(void)
{
  uint32_t pulses;
  uint32_t primask = Critical_Enter();
  pulses = frequency_pulse_count;
  frequency_pulse_count = 0U;
  Critical_Exit(primask);
  return pulses;
}

static uint32_t Rotation_TakeCheckPulses(void)
{
  uint32_t pulses;
  uint32_t primask = Critical_Enter();
  pulses = rotation_check_pulse_count;
  rotation_check_pulse_count = 0U;
  Critical_Exit(primask);
  return pulses;
}

static bool App_StartMeasurements(uint32_t now)
{
  uint32_t primask;

  App_StopMeasurements();
  App_EnableMeasurementClocks();
  memset(adc_buffer, 0, sizeof(adc_buffer));
  App_ResetRmsAccumulator();
  rms_valid = false;
  Rotation_ResetCounters();

  primask = Critical_Enter();
  adc_dma_pending_mask = 0U;
  adc_dma_overrun = 0U;
  adc_dma_error = 0U;
  adc_dma_generation = 0U;
  Critical_Exit(primask);

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)adc_buffer,
                        ADC_DMA_BUFFER_VALUES) != HAL_OK)
  {
    Debug_Log("[MEAS] HAL_ADC_Start_DMA failed, err=0x%08lX\r\n",
              HAL_ADC_GetError(&hadc1));
    (void)HAL_ADC_Stop_DMA(&hadc1);
    device_status_flags |= STATUS_ADC_WINDOW_ERROR;
    return false;
  }

  if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
  {
    Debug_Log("[MEAS] HAL_TIM_Base_Start failed\r\n");
    (void)HAL_ADC_Stop_DMA(&hadc1);
    device_status_flags |= STATUS_ADC_WINDOW_ERROR;
    return false;
  }

  measurements_running = true;
  Rotation_SetExtiEnabled(true);
  rotation_report_tick = now + ROTATION_REPORT_WINDOW_MS;
  rotation_check_tick = now + ROTATION_CHECK_WINDOW_MS;

  App_LogSamplingConfiguration();
  Debug_Log("[MEAS] conversion constants: Vref=%ld mV, midpoint=%ld mV, samples=%u\r\n",
            (int32_t)(VREF * 1000.0f),
            (int32_t)(ANALOG_MIDPOINT_V * 1000.0f),
            (unsigned int)RMS_SAMPLES);
  return true;
}

static void App_StopMeasurements(void)
{
  Rotation_SetExtiEnabled(false);

  if (measurement_clocks_enabled)
  {
    (void)HAL_TIM_Base_Stop(&htim2);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
  }
  measurements_running = false;
  App_DisableMeasurementClocks();
  ds18b20_state = DS18B20_STATE_IDLE;
}

static void App_EnableMeasurementClocks(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_ADC2_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();
  HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);
  measurement_clocks_enabled = true;
}

static void App_DisableMeasurementClocks(void)
{
  HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_DisableIRQ(TIM2_IRQn);
  __HAL_RCC_TIM2_CLK_DISABLE();
  __HAL_RCC_ADC1_CLK_DISABLE();
  __HAL_RCC_ADC2_CLK_DISABLE();
  __HAL_RCC_DMA2_CLK_DISABLE();
  measurement_clocks_enabled = false;
}

static void App_CalculateRmsWindow(void)
{
  static const uint8_t voltage_index[3] = {ADC_IDX_VA, ADC_IDX_VB, ADC_IDX_VC};
  static const uint8_t current_index[3] = {ADC_IDX_IA, ADC_IDX_IB, ADC_IDX_IC};

  for (uint8_t i = 0; i < 3U; i++)
  {
    float mean_v = sum[voltage_index[i]] / (float)RMS_SAMPLES;
    float var_v = (sum_sq[voltage_index[i]] / (float)RMS_SAMPLES) - (mean_v * mean_v);
    float mean_i = sum[current_index[i]] / (float)RMS_SAMPLES;
    float var_i = (sum_sq[current_index[i]] / (float)RMS_SAMPLES) - (mean_i * mean_i);
    int32_t mean_v_mv;
    int32_t mean_i_mv;

    if (var_v < 0.0f)
    {
      var_v = 0.0f;
    }
    if (var_i < 0.0f)
    {
      var_i = 0.0f;
    }

    rms_voltage[i] = sqrtf(var_v) * (VREF / ADC_MAX) / VOLTAGE_GAIN_TOTAL;
    rms_current[i] = sqrtf(var_i) * (VREF / ADC_MAX) / CURRENT_V_PER_AMP;

    mean_v_mv = (int32_t)((mean_v * VREF * 1000.0f) / ADC_MAX);
    mean_i_mv = (int32_t)((mean_i * VREF * 1000.0f) / ADC_MAX);
    Debug_Log("[MEAS] phase %u mean: V=%ld mV, I=%ld mV, midpoint=%ld mV\r\n",
              (unsigned int)(i + 1U), mean_v_mv, mean_i_mv,
              (int32_t)(ANALOG_MIDPOINT_V * 1000.0f));
    Debug_Log("[MEAS] phase %u rms: U=%ld.%02ld V, I=%ld.%02ld A\r\n",
              (unsigned int)(i + 1U),
              (int32_t)rms_voltage[i],
              (int32_t)((rms_voltage[i] - (float)((int32_t)rms_voltage[i])) * 100.0f),
              (int32_t)rms_current[i],
              (int32_t)((rms_current[i] - (float)((int32_t)rms_current[i])) * 100.0f));
  }

  rms_valid = true;
  device_status_flags &= (uint8_t)~STATUS_ADC_WINDOW_ERROR;
}

static void App_ProcessAdcBlock(uint16_t offset)
{
  for (uint16_t sequence = 0U; sequence < ADC_DMA_HALF_SEQUENCES; sequence++)
  {
    uint16_t base = (uint16_t)(offset + (sequence * ADC_CHANNELS));

    for (uint8_t channel = 0U; channel < ADC_CHANNELS; channel++)
    {
      uint16_t raw = adc_buffer[base + channel];
      float value = (float)raw;

      if ((raw == 0U) || (raw >= (uint16_t)ADC_MAX))
      {
        device_status_flags |= STATUS_ADC_SATURATION;
      }

      sum[channel] += value;
      sum_sq[channel] += value * value;
    }

    rms_count++;
    if (rms_count >= RMS_SAMPLES)
    {
      App_CalculateRmsWindow();
      App_ResetRmsAccumulator();
    }
  }

}

static bool App_AdcGenerationStable(uint32_t generation)
{
  bool stable;
  uint32_t primask = Critical_Enter();

  stable = AppLogic_AdcGenerationStable(generation, adc_dma_generation);
  if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_OVR) != RESET)
  {
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
    adc_dma_generation++;
    adc_dma_error = 1U;
    stable = false;
  }
  Critical_Exit(primask);
  return stable;
}

static bool App_RestartAdcAcquisition(void)
{
  uint32_t primask;

  /* Preserve the independent rotation window.  Only the corrupted ADC/RMS
     acquisition is restarted after a hardware/DMA error. */
  measurements_running = false;
  HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
  (void)HAL_TIM_Base_Stop(&htim2);
  (void)HAL_ADC_Stop_DMA(&hadc1);
  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);

  primask = Critical_Enter();
  adc_dma_pending_mask = 0U;
  adc_dma_overrun = 0U;
  adc_dma_error = 0U;
  adc_dma_generation = 0U;
  Critical_Exit(primask);
  HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)adc_buffer,
                        ADC_DMA_BUFFER_VALUES) != HAL_OK)
  {
    Debug_Log("[MEAS] ADC/DMA recovery start failed, err=0x%08lX\r\n",
              HAL_ADC_GetError(&hadc1));
    return false;
  }

  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
  {
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    Debug_Log("[MEAS] TIM2 recovery start failed\r\n");
    return false;
  }

  measurements_running = true;
  return true;
}

static void App_ProcessAdcDma(void)
{
  AppLogicAdcBlockAction action;
  uint8_t pending;
  uint8_t overrun;
  uint8_t error;
  uint32_t generation;
  uint32_t primask = Critical_Enter();

  if (measurements_running &&
      (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_OVR) != RESET))
  {
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
    adc_dma_generation++;
    adc_dma_error = 1U;
  }

  pending = adc_dma_pending_mask;
  overrun = adc_dma_overrun;
  error = adc_dma_error;
  generation = adc_dma_generation;
  adc_dma_pending_mask = 0U;
  adc_dma_overrun = 0U;
  adc_dma_error = 0U;
  Critical_Exit(primask);

  if (!measurements_running)
  {
    return;
  }

  action = AppLogic_AdcSelectBlock(error, overrun, pending);
  if (action == APP_LOGIC_ADC_BLOCK_NONE)
  {
    /* The main loop normally runs many times between DMA callbacks. */
    return;
  }

  if (action == APP_LOGIC_ADC_BLOCK_DISCARD)
  {
    if (error != 0U)
    {
      Debug_Log("[MEAS] ADC/DMA error; discard only RMS window and restart acquisition\r\n");
    }
    else if (overrun != 0U)
    {
      Debug_Log("[MEAS] repeated DMA half/full callback; discard overwritten window\r\n");
    }
    else
    {
      Debug_Log("[MEAS] both DMA halves pending; discard window already being overwritten\r\n");
    }
    device_status_flags |= STATUS_ADC_WINDOW_ERROR;
    App_ResetRmsAccumulator();
    rms_valid = false;

    if (error != 0U)
    {
      if (!App_RestartAdcAcquisition())
      {
        App_RequestReconnect(HAL_GetTick(), "ADC restart failed");
      }
    }
    return;
  }

  if (action == APP_LOGIC_ADC_BLOCK_HALF)
  {
    App_ProcessAdcBlock(0U);
    if (!App_AdcGenerationStable(generation))
    {
      Debug_Log("[MEAS] DMA advanced while half-buffer was processed; discard RMS window\r\n");
      device_status_flags |= STATUS_ADC_WINDOW_ERROR;
      App_ResetRmsAccumulator();
      rms_valid = false;
      return;
    }
  }
  else if (action == APP_LOGIC_ADC_BLOCK_FULL)
  {
    App_ProcessAdcBlock(ADC_DMA_HALF_VALUES);
    if (!App_AdcGenerationStable(generation))
    {
      Debug_Log("[MEAS] DMA advanced while full-buffer half was processed; discard RMS window\r\n");
      device_status_flags |= STATUS_ADC_WINDOW_ERROR;
      App_ResetRmsAccumulator();
      rms_valid = false;
    }
  }
}

static void App_ServiceRotation(uint32_t now)
{
  if (TimeReached(now, rotation_report_tick))
  {
    uint32_t pulses = Rotation_TakeReportPulses();
    revolutions = (float)pulses / (float)PULSES_PER_REV;
    rotation_report_tick = now + ROTATION_REPORT_WINDOW_MS;
    Debug_Log("[ROT] report window=%lu ms pulses=%lu revolutions_x100=%ld\r\n",
              (uint32_t)ROTATION_REPORT_WINDOW_MS, pulses,
              (int32_t)(revolutions * 100.0f));
  }

  if (TimeReached(now, rotation_check_tick))
  {
    uint32_t pulses = Rotation_TakeCheckPulses();
    rotation_check_tick = now + ROTATION_CHECK_WINDOW_MS;
    Debug_Log("[ROT] stop-check window=%lu ms pulses=%lu\r\n",
              (uint32_t)ROTATION_CHECK_WINDOW_MS, pulses);

    if (AppLogic_RotationStopped(true, pulses) &&
        (battery_request == BATTERY_REASON_NONE))
    {
      Battery_Start(BATTERY_REASON_ROTATION_STOP, now);
    }
  }
}

static void Battery_Start(BatteryReason reason, uint32_t now)
{
  if (battery_request != BATTERY_REASON_NONE)
  {
    return;
  }

  battery_request = reason;
  /* Project history powers the ADC2_IN3 battery divider/detector through
     ON_PWR_DET.  No schematic is stored in this repository, so preserve that
     established board mapping rather than guessing ON_AX or ON_AN. */
  HAL_GPIO_WritePin(ON_PWR_DET_GPIO_Port, ON_PWR_DET_Pin, GPIO_PIN_SET);
  battery_deadline = now + BATTERY_SETTLE_MS;
  Debug_Log("[BAT] measurement requested, reason=%u, settle=%lu ms\r\n",
            (unsigned int)reason, (uint32_t)BATTERY_SETTLE_MS);
}

static void Battery_Service(uint32_t now)
{
  if ((battery_request != BATTERY_REASON_NONE) && TimeReached(now, battery_deadline))
  {
    battery_completed_reason = battery_request;
    battery_completed_result = Battery_ReadReliable();
    if (!measurement_clocks_enabled)
    {
      (void)HAL_ADC_Stop(&hadc2);
      __HAL_RCC_ADC2_CLK_DISABLE();
    }
    battery_request = BATTERY_REASON_NONE;
    battery_result_ready = true;
  }
}

static void Battery_HandleCompleted(uint32_t now)
{
  BatteryReason reason;
  BatteryResult result;

  if (!battery_result_ready)
  {
    return;
  }

  reason = battery_completed_reason;
  result = battery_completed_result;
  battery_result_ready = false;
  battery_completed_reason = BATTERY_REASON_NONE;

  if (result.valid)
  {
    adc_bat_raw = result.raw;
    battery_voltage = result.voltage;
    device_status_flags &= (uint8_t)~STATUS_BATTERY_INVALID;
    if ((result.raw == 0U) || (result.raw >= (uint16_t)ADC_MAX))
    {
      device_status_flags |= STATUS_ADC_SATURATION;
    }
    Debug_Log("[BAT] valid: %ld.%02ld V raw=%u reason=%u\r\n",
              (int32_t)battery_voltage,
              (int32_t)((battery_voltage - (float)((int32_t)battery_voltage)) * 100.0f),
              adc_bat_raw, (unsigned int)reason);

    if (AppLogic_BatteryLow(result.valid, battery_voltage, BAT_LOW_THRESHOLD_V) &&
        (reason != BATTERY_REASON_ROTATION_STOP) &&
        (reason != BATTERY_REASON_WAKE_ROTATION_RESULT))
    {
      Debug_Log("[BAT] below 3.6 V; shutdown deferred until zero rotation is confirmed\r\n");
    }
  }
  else
  {
    device_status_flags |= STATUS_BATTERY_INVALID;
    Debug_Log("[BAT] invalid after %u ADC retries; no false low-voltage decision\r\n",
              (unsigned int)BATTERY_ADC_RETRIES);
  }

  if ((app_state != APP_STATE_ACTIVE) && (app_state != APP_STATE_ACTIVE_POWER_SETTLE))
  {
    HAL_GPIO_WritePin(ON_PWR_DET_GPIO_Port, ON_PWR_DET_Pin, GPIO_PIN_RESET);
  }

  switch (reason)
  {
    case BATTERY_REASON_JOIN_FAILURES:
      next_join_battery_tick = now + BATTERY_ACTIVE_PERIOD_MS;
      if (join_confirmed_pending)
      {
        join_confirmed_pending = false;
        App_StartActiveAfterJoin(now, "late JOINED while checking battery");
      }
      else
      {
        join_state = JOIN_STATE_WAIT_RESULT;
        join_deadline = now + RAK_JOIN_RESULT_TIMEOUT_MS;
      }
      break;

    case BATTERY_REASON_JOIN_PERIODIC:
      next_join_battery_tick = now +
        (result.valid ? BATTERY_ACTIVE_PERIOD_MS : BATTERY_ERROR_RETRY_MS);
      if (join_confirmed_pending)
      {
        join_confirmed_pending = false;
        App_StartActiveAfterJoin(now, "late JOINED while checking battery");
      }
      else if (join_state == JOIN_STATE_BATTERY_CHECK)
      {
        join_state = JOIN_STATE_WAIT_RESULT;
        join_deadline = now + RAK_JOIN_RESULT_TIMEOUT_MS;
      }
      break;

    case BATTERY_REASON_ACTIVE_PERIODIC:
    case BATTERY_REASON_PWR_INPUT:
      next_battery_tick = now + (result.valid ? BATTERY_ACTIVE_PERIOD_MS : BATTERY_ERROR_RETRY_MS);
      break;

    case BATTERY_REASON_ROTATION_STOP:
      if (result.valid)
      {
        uint32_t new_pulses = Rotation_TakeCheckPulses();
        if (AppLogic_ShouldShutdown(true, battery_voltage, BAT_LOW_THRESHOLD_V,
                                    true, new_pulses))
        {
          App_EnterEmergency("battery below 3.6 V and zero rotation confirmed");
        }
        else
        {
          Debug_Log("[PWR] shutdown cancelled: battery_x100=%ld, new rotation pulses=%lu; "
                    "both low battery and zero rotation are required\r\n",
                    (int32_t)(battery_voltage * 100.0f), new_pulses);
          rotation_check_tick = now + ROTATION_CHECK_WINDOW_MS;
        }
      }
      else
      {
        next_battery_tick = now + BATTERY_ERROR_RETRY_MS;
      }
      break;

    case BATTERY_REASON_WAKEUP:
      if (result.valid)
      {
        App_SetExternalPower(false, false, true, false, false);
        app_deadline = now + MEASUREMENT_POWER_SETTLE_MS;
        App_SetState(APP_STATE_WAKE_ROTATION_SETTLE, "wake battery OK; power ON_FR briefly");
      }
      else
      {
        app_deadline = now + BATTERY_ERROR_RETRY_MS;
      }
      break;

    case BATTERY_REASON_WAKE_ROTATION_RESULT:
      if (result.valid)
      {
        if (AppLogic_ShouldShutdown(true, battery_voltage, BAT_LOW_THRESHOLD_V,
                                    true, wake_rotation_pulses))
        {
          App_EnterEmergency("wake check confirmed low battery and zero rotation");
        }
        else
        {
          next_join_battery_tick = now + BATTERY_ACTIVE_PERIOD_MS;
          App_StartRakBoot(now);
        }
      }
      else
      {
        app_deadline = now + BATTERY_ERROR_RETRY_MS;
      }
      break;

    case BATTERY_REASON_NONE:
    default:
      break;
  }
}

static void App_StartRakBoot(uint32_t now)
{
  App_StopMeasurements();
  DS18B20_LineSafeOff();
  App_SetExternalPower(true, false, false, false, false);

  if (!RAK_UartStart())
  {
    Debug_Log("[RAK] UART start failed; retry power cycle\r\n");
    RAK_UartStop();
    App_SetExternalPower(false, false, false, false, false);
    app_deadline = now + RAK_POWER_OFF_DELAY_MS;
    App_SetState(APP_STATE_RAK_OFF_WAIT, "RAK UART unavailable");
    return;
  }

  app_deadline = now + RAK_BOOT_DELAY_MS;
  App_SetState(APP_STATE_RAK_BOOT, "RAK power enabled");
  Debug_Log("[RAK] boot wait=%lu ms\r\n", (uint32_t)RAK_BOOT_DELAY_MS);
}

static void App_StartActiveAfterJoin(uint32_t now, const char *confirmation)
{
  if ((battery_request == BATTERY_REASON_JOIN_FAILURES) ||
      (battery_request == BATTERY_REASON_JOIN_PERIODIC))
  {
    join_confirmed_pending = true;
    Debug_Log("[RAK] JOIN confirmed by %s; finish pending battery check first\r\n", confirmation);
    return;
  }

  join_confirmed_pending = false;
  join_failures_since_battery = 0U;
  next_join_battery_tick = 0U;
  consecutive_send_failures = 0U;
  uplink_state = UPLINK_STATE_IDLE;
  App_SetExternalPower(true, true, true, true, true);
  app_deadline = now + MEASUREMENT_POWER_SETTLE_MS;
  App_SetState(APP_STATE_ACTIVE_POWER_SETTLE, confirmation);
  LED_Indicate(true, now);
}

static void App_RequestReconnect(uint32_t now, const char *reason)
{
  Debug_Log("[RAK] reconnect requested: %s\r\n", reason);
  device_status_flags |= STATUS_JOIN_LOST;
  App_StopMeasurements();
  RAK_UartStop();
  DS18B20_LineSafeOff();
  App_SetExternalPower(false, false, false, false, false);
  app_deadline = now + RAK_POWER_OFF_DELAY_MS;
  join_state = JOIN_STATE_IDLE;
  uplink_state = UPLINK_STATE_IDLE;
  next_join_battery_tick = now + BATTERY_ACTIVE_PERIOD_MS;
  App_SetState(APP_STATE_RAK_OFF_WAIT, reason);
}

static void App_EnterStopCycle(void)
{
  uint32_t fallback_deadline;
  uint32_t stop_primask;
  bool used_stop = false;

  App_StopMeasurements();
  RAK_UartStop();
  DS18B20_LineSafeOff();
  App_SetExternalPower(false, false, false, false, false);
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  /* EXTI12 can latch a pending edge even while its IMR bit is masked. */
  EXTI->PR = (uint32_t)Freqency_Pin;
  __DSB();
  Debug_Log("[PWR] enter low power; nominal RTC wake=%u s (uncalibrated LSI tolerance applies)\r\n",
            (unsigned int)STOP_WAKEUP_SECONDS);
  debug_uart_ready = false;
  __HAL_RCC_USART2_CLK_DISABLE();

  if (rtc_wakeup_ready && RTC_WakeupStart(STOP_WAKEUP_SECONDS))
  {
    used_stop = true;
    HAL_SuspendTick();
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    /* PB12 can latch again after the earlier shutdown clear.  Make the final
       line-only clear atomic with WFI without touching other EXTI10..15 lines.
       Any other enabled interrupt may still wake WFI and is serviced after
       PRIMASK is restored. */
    stop_primask = Critical_Enter();
    EXTI->PR = (uint32_t)Freqency_Pin;
    __DSB();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    Critical_Exit(stop_primask);
    /* STOP selects HSI as SYSCLK.  Rebuild a temporary 16 MHz HAL time base
       before waiting for HSE/PLL, then rebuild the final 72 MHz time base.
       Using the stale pre-STOP SysTick reload would shorten RCC timeouts. */
    SystemCoreClockUpdate();
    if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK)
    {
      Error_Handler();
    }
    HAL_ResumeTick();
    SystemClock_Config();
    SystemCoreClockUpdate();
    if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK)
    {
      Error_Handler();
    }
    HAL_ResumeTick();
    DWT_Delay_Init();
    RTC_WakeupStop();
  }
  else
  {
    fallback_deadline = HAL_GetTick() + (STOP_WAKEUP_SECONDS * 1000U);
    while (!TimeReached(HAL_GetTick(), fallback_deadline))
    {
      __WFI();
    }
  }

  __HAL_RCC_USART2_CLK_ENABLE();
  debug_uart_ready = true;
  if (used_stop)
  {
    Debug_Log("[PWR] STOP wake, source=%s, clocks restored SYSCLK=%lu\r\n",
              (rtc_wakeup_fired != 0U) ? "RTC WUT" : "external IRQ",
              HAL_RCC_GetSysClockFreq());
  }
  else
  {
    Debug_Log("[PWR] RTC WUT unavailable; interrupt-sleep fallback completed\r\n");
  }

  app_deadline = HAL_GetTick();
  wake_battery_reason = BATTERY_REASON_WAKEUP;
  App_SetState(APP_STATE_WAKE_BATTERY, "periodic low-power wake");
}

static void App_EnterEmergency(const char *reason)
{
  app_state = APP_STATE_EMERGENCY;
  HAL_ResumeTick();
  Debug_Log("[FAULT] emergency shutdown: %s\r\n", reason);
  App_StopMeasurements();
  if (rtc_wakeup_ready)
  {
    RTC_WakeupStop();
  }
  RAK_UartStop();
  DS18B20_LineSafeOff();
  App_SetExternalPower(false, false, false, false, false);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10 | GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  debug_uart_ready = false;
  __HAL_RCC_USART2_CLK_DISABLE();

  /* A real board removes the STM32 supply at the first PWR_OFF low edge.
     Later loop iterations execute only if the external power circuit restores it. */
  while (1)
  {
    HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
    HAL_Delay(1000U);
    HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_RESET);
    HAL_Delay(1000U);
  }
}

static void App_Init(void)
{
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  App_SetExternalPower(true, false, false, false, false);
  App_StopMeasurements();
  Rotation_SetExtiEnabled(false);
  DS18B20_LineSafeOff();
  RAK_ResetParser();
  next_join_battery_tick = HAL_GetTick() + BATTERY_ACTIVE_PERIOD_MS;

  rtc_wakeup_ready = RTC_WakeupInit();
  Debug_Log("[BOOT] RTC WUT/LSI %s; LSI nominal=%lu Hz, wake=%u s nominal only\r\n",
            rtc_wakeup_ready ? "ready" : "failed", (uint32_t)LSI_VALUE,
            (unsigned int)STOP_WAKEUP_SECONDS);
  Debug_Log("[BOOT] zero-rotation cycle is about %lu ms: STOP nominal + ON_FR observation\r\n",
            (uint32_t)((STOP_WAKEUP_SECONDS * 1000U) +
                       ROTATION_CHECK_WINDOW_MS));
  Debug_Log("[BOOT] state machine ready; PWR_OFF held HIGH\r\n");
  app_state = APP_STATE_STARTUP;
}

static void App_Run(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t events;

  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  RAK_ProcessRx();
  events = rak_events;
  rak_events = 0U;
  LED_Service(now);
  App_ProcessAdcDma();
  Battery_Service(now);
  Battery_HandleCompleted(now);

  if (((app_state == APP_STATE_STARTUP) ||
       (app_state == APP_STATE_RAK_OFF_WAIT) ||
       (app_state == APP_STATE_RAK_BOOT) ||
       (app_state == APP_STATE_JOINING)) &&
      TimeReached(now, next_join_battery_tick) &&
      (battery_request == BATTERY_REASON_NONE))
  {
    Battery_Start(BATTERY_REASON_JOIN_PERIODIC, now);
    next_join_battery_tick = now + BATTERY_ACTIVE_PERIOD_MS;
  }

  if (app_state == APP_STATE_ACTIVE)
  {
    DS18B20_Service(now);
  }

  switch (app_state)
  {
    case APP_STATE_STARTUP:
      App_StartRakBoot(now);
      break;

    case APP_STATE_RAK_OFF_WAIT:
      if (TimeReached(now, app_deadline) &&
          (battery_request == BATTERY_REASON_NONE))
      {
        App_StartRakBoot(now);
      }
      break;

    case APP_STATE_RAK_BOOT:
      if (AppLogic_JoinConfirmed(events))
      {
        App_StartActiveAfterJoin(now, "+EVT:JOINED during RAK boot");
      }
      else if (TimeReached(now, app_deadline))
      {
        RAK_ResetParser();
        join_state = JOIN_STATE_IDLE;
        App_SetState(APP_STATE_JOINING, "RAK boot complete; stale partial UART line cleared");
        RAK_StartJoinAttempt(now);
      }
      break;

    case APP_STATE_JOINING:
      RAK_HandleJoin(now, events);
      break;

    case APP_STATE_ACTIVE_POWER_SETTLE:
      if ((events & (RAK_EVT_NO_NETWORK | RAK_EVT_NJS_0)) != 0U)
      {
        App_RequestReconnect(now, "network lost while measurement power settled");
        break;
      }
      if (TimeReached(now, app_deadline))
      {
        DS18B20_LineConfigureActive();
        if (App_StartMeasurements(now))
        {
          ds18b20_discovery_tick = now;
          DS18B20_DiscoverCached(now);
          ds18b20_next_tick = now;
          next_battery_tick = now;
          next_uplink_tick = now;
          Debug_Log("[RAK] first uplink armed; waiting for first valid measurement window\r\n");
          pwr_input_low_latched = false;
          App_SetState(APP_STATE_ACTIVE, "measurement power settled");
        }
        else
        {
          App_RequestReconnect(now, "measurement start failed");
        }
      }
      break;

    case APP_STATE_ACTIVE:
    {
      uint8_t payload[RAK_MEAS_PAYLOAD_SIZE] = {0};

      RAK_HandleUplink(now, events);
      if (app_state != APP_STATE_ACTIVE)
      {
        break;
      }

      App_ServiceRotation(now);

      if (HAL_GPIO_ReadPin(PWR_GPIO_Port, PWR_Pin) == GPIO_PIN_RESET)
      {
        if (!pwr_input_low_latched && (battery_request == BATTERY_REASON_NONE))
        {
          pwr_input_low_latched = true;
          Battery_Start(BATTERY_REASON_PWR_INPUT, now);
        }
      }
      else
      {
        pwr_input_low_latched = false;
      }

      if (TimeReached(now, next_battery_tick) &&
          (battery_request == BATTERY_REASON_NONE))
      {
        Battery_Start(BATTERY_REASON_ACTIVE_PERIODIC, now);
      }

      if ((uplink_state == UPLINK_STATE_IDLE) && TimeReached(now, next_uplink_tick) &&
          rms_valid && (battery_request == BATTERY_REASON_NONE) &&
          (ds18b20_state == DS18B20_STATE_IDLE))
      {
        (void)ReadAuxAnalogInputs();
        if (BuildMeasurementPayload(payload, sizeof(payload)) &&
            RAK_SendBytesPayload(2U, payload, sizeof(payload)))
        {
          uplink_state = UPLINK_STATE_WAIT_ACCEPT;
          uplink_deadline = now + RAK_AT_TIMEOUT_MS;
          Debug_Log("[RAK] uplink command accepted for transmission processing\r\n");
        }
        else
        {
          RAK_CompleteUplinkFailure(now, "AT+SEND UART transmit failed");
        }
      }
      break;
    }

    case APP_STATE_STOP:
      App_EnterStopCycle();
      break;

    case APP_STATE_WAKE_BATTERY:
      if ((battery_request == BATTERY_REASON_NONE) && TimeReached(now, app_deadline))
      {
        Battery_Start(wake_battery_reason, now);
      }
      break;

    case APP_STATE_WAKE_ROTATION_SETTLE:
      if (TimeReached(now, app_deadline))
      {
        Rotation_ResetCounters();
        Rotation_SetExtiEnabled(true);
        app_deadline = now + ROTATION_CHECK_WINDOW_MS;
        App_SetState(APP_STATE_WAKE_ROTATION_CHECK, "ON_FR settled; fresh rotation window started");
      }
      break;

    case APP_STATE_WAKE_ROTATION_CHECK:
      if (TimeReached(now, app_deadline))
      {
        wake_rotation_pulses = Rotation_TakeCheckPulses();
        Rotation_SetExtiEnabled(false);
        HAL_GPIO_WritePin(ON_FR_GPIO_Port, ON_FR_Pin, GPIO_PIN_RESET);
        /* A final line-only clear prevents a late PB12 edge from causing an
           immediate wake when the following STOP cycle begins. */
        EXTI->PR = (uint32_t)Freqency_Pin;
        __DSB();
        Debug_Log("[ROT] wake check complete: pulses=%lu window=%lu ms\r\n",
                  wake_rotation_pulses, (uint32_t)ROTATION_CHECK_WINDOW_MS);
        wake_battery_reason = BATTERY_REASON_WAKE_ROTATION_RESULT;
        Battery_Start(wake_battery_reason, now);
        App_SetState(APP_STATE_WAKE_BATTERY,
                     "rotation window complete; verify battery before STOP or JOIN");
      }
      break;

    case APP_STATE_EMERGENCY:
    default:
      App_EnterEmergency("invalid state");
      break;
  }
}

static void TextAppend(char *dst, uint16_t dst_size, uint16_t *pos, const char *src)
{
  while ((*src != '\0') && ((*pos + 1U) < dst_size))
  {
    dst[*pos] = *src;
    (*pos)++;
    src++;
  }
  dst[*pos] = '\0';
}

static void TextAppendInt32(char *dst, uint16_t dst_size, uint16_t *pos, int32_t value)
{
  char buf[12] = {0};
  uint8_t buf_pos = sizeof(buf);
  uint32_t number;

  buf[--buf_pos] = '\0';

  if (value < 0)
  {
    number = (uint32_t)(-value);
  }
  else
  {
    number = (uint32_t)value;
  }

  do
  {
    buf[--buf_pos] = (char)('0' + (number % 10U));
    number /= 10U;
  } while (number > 0U);

  if (value < 0)
  {
    buf[--buf_pos] = '-';
  }

  TextAppend(dst, dst_size, pos, &buf[buf_pos]);
}

static bool RAK_UartStart(void)
{
  if (!rak_uart_initialized)
  {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&RAK_UART) != HAL_OK)
    {
      return false;
    }
    rak_uart_initialized = true;
  }

  RAK_ResetParser();
  HAL_NVIC_SetPriority(USART1_IRQn, 1, 1);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  rak_uart_rx_enabled = true;
  __HAL_UART_CLEAR_OREFLAG(&RAK_UART);
  if (HAL_UART_Receive_IT(&RAK_UART, &rak_rx_byte, 1U) != HAL_OK)
  {
    rak_uart_rx_enabled = false;
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    return false;
  }

  return true;
}

static void RAK_UartStop(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  rak_uart_rx_enabled = false;
  HAL_NVIC_DisableIRQ(USART1_IRQn);
  if (rak_uart_initialized)
  {
    (void)HAL_UART_AbortReceive(&RAK_UART);
    (void)HAL_UART_DeInit(&RAK_UART);
    rak_uart_initialized = false;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  RAK_ResetParser();
}

static void RAK_ResetParser(void)
{
  uint32_t primask = Critical_Enter();
  rak_rx_tail = rak_rx_head;
  rak_rx_overflow = 0U;
  rak_rx_error = 0U;
  rak_rx_fault_pending = 0U;
  rak_rx_fault_boundary = rak_rx_head;
  Critical_Exit(primask);

  rak_rx_line_length = 0U;
  rak_rx_discard_line = false;
  rak_rx_buffer[0] = '\0';
  rak_events = 0U;
}

static uint32_t RAK_ClassifyLine(const char *line)
{
  bool njs_reply_expected = (app_state == APP_STATE_JOINING) &&
                            (join_state == JOIN_STATE_WAIT_STATUS);
  return AppLogic_RakClassifyLineWithNjsContext(line, njs_reply_expected);
}

static void RAK_ProcessRx(void)
{
  bool report_fault = false;
  bool restart_rx = false;
  bool fault_boundary_valid = false;
  uint16_t fault_boundary = 0U;
  uint32_t primask = Critical_Enter();

  if ((rak_rx_overflow != 0U) || (rak_rx_error != 0U) ||
      (rak_rx_fault_pending != 0U))
  {
    restart_rx = (rak_rx_error != 0U);
    fault_boundary_valid = (rak_rx_fault_pending != 0U);
    fault_boundary = rak_rx_fault_boundary;
    rak_rx_overflow = 0U;
    rak_rx_error = 0U;
    rak_rx_fault_pending = 0U;
    device_status_flags |= STATUS_UART_OVERFLOW;
    report_fault = true;
  }
  Critical_Exit(primask);

  if (restart_rx && rak_uart_rx_enabled)
  {
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    (void)HAL_UART_AbortReceive(&RAK_UART);
    __HAL_UART_CLEAR_OREFLAG(&RAK_UART);
    if (HAL_UART_Receive_IT(&RAK_UART, &rak_rx_byte, 1U) != HAL_OK)
    {
      primask = Critical_Enter();
      if (rak_rx_fault_pending == 0U)
      {
        rak_rx_fault_boundary = rak_rx_head;
        rak_rx_fault_pending = 1U;
      }
      rak_rx_error = 1U;
      Critical_Exit(primask);
    }
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  }

  if (fault_boundary_valid && (rak_rx_tail == fault_boundary))
  {
    rak_rx_discard_line = true;
    rak_rx_line_length = 0U;
    fault_boundary_valid = false;
  }

  while (rak_rx_tail != rak_rx_head)
  {
    uint8_t byte = rak_rx_ring[rak_rx_tail];
    rak_rx_tail = (uint16_t)((rak_rx_tail + 1U) & (RAK_RX_RING_SIZE - 1U));

    if ((byte == '\r') || (byte == '\n'))
    {
      if (rak_rx_discard_line)
      {
        rak_rx_discard_line = false;
        rak_rx_line_length = 0U;
      }
      else if (rak_rx_line_length > 0U)
      {
        uint32_t event;
        rak_rx_buffer[rak_rx_line_length] = '\0';
        event = RAK_ClassifyLine(rak_rx_buffer);
        if (event != 0U)
        {
          rak_events |= event;
          Debug_Log("[RAK] RX event: %s\r\n", rak_rx_buffer);
        }
        else
        {
          Debug_Log("[RAK] RX line: %s\r\n", rak_rx_buffer);
        }
        rak_rx_line_length = 0U;
      }
    }
    else if (rak_rx_discard_line)
    {
    }
    else if (rak_rx_line_length < (RAK_RX_BUFFER_SIZE - 1U))
    {
      rak_rx_buffer[rak_rx_line_length++] = (char)byte;
    }
    else
    {
      rak_rx_discard_line = true;
      rak_rx_line_length = 0U;
      device_status_flags |= STATUS_UART_OVERFLOW;
      report_fault = true;
    }

    if (fault_boundary_valid && (rak_rx_tail == fault_boundary))
    {
      /* Bytes before the ISR snapshot were intact and have now been parsed.
         Only the line crossing the actual UART/ring fault is discarded. */
      rak_rx_discard_line = true;
      rak_rx_line_length = 0U;
      break;
    }
  }

  if (report_fault)
  {
    Debug_Log("[RAK] UART overflow/error: damaged line discarded, parser resynchronized at EOL\r\n");
  }
}

static bool RAK_SendCommand(const char *cmd)
{
  uint16_t length = (uint16_t)strlen(cmd);

  Debug_Log("[RAK] TX: %s", cmd);
  if (HAL_UART_Transmit(&RAK_UART, (uint8_t *)cmd, length,
                        RAK_UART_TX_TIMEOUT_MS) != HAL_OK)
  {
    Debug_Log("[RAK] TX failed, err=0x%08lX\r\n", HAL_UART_GetError(&RAK_UART));
    return false;
  }
  return true;
}

static bool RAK_SendJoinCommand(void)
{
  /* RUI3 limits one command to 255 automatic attempts.  Auto-join remains
     enabled across power cycles, and the STM32 resubmits this command forever
     until the exact asynchronous +EVT:JOINED line is received. */
  return RAK_SendCommand("AT+JOIN=1:1:10:255\r\n");
}

static bool RAK_SendNetworkStatusCommand(void)
{
  return RAK_SendCommand("AT+NJS=?\r\n");
}

static void RAK_StartJoinAttempt(uint32_t now)
{
  join_status_after_busy = false;
  if (RAK_SendJoinCommand())
  {
    join_attempt_number++;
    join_state = JOIN_STATE_WAIT_ACCEPT;
    join_deadline = now + RAK_JOIN_ACCEPT_TIMEOUT_MS;
    Debug_Log("[RAK] JOIN attempt #%lu started; failures=%lu\r\n",
              join_attempt_number, join_failed_attempts);
  }
  else
  {
    Debug_Log("[RAK] JOIN command TX failed; power-cycle RAK\r\n");
    App_RequestReconnect(now, "JOIN command UART transmit failed");
  }
}

static void RAK_RecordJoinFailure(uint32_t now, const char *reason)
{
  join_failed_attempts++;
  join_failures_since_battery++;
  Debug_Log("[RAK] JOIN failure event #%lu (%u/%u before battery check): %s; "
            "RAK automatic retries remain active\r\n",
            join_failed_attempts, (unsigned int)join_failures_since_battery,
            (unsigned int)RAK_JOIN_FAILURES_PER_BATTERY_CHECK, reason);
  LED_Indicate(false, now);

  if (join_failures_since_battery >= RAK_JOIN_FAILURES_PER_BATTERY_CHECK)
  {
    join_failures_since_battery = 0U;
    join_state = JOIN_STATE_BATTERY_CHECK;
    if (battery_request == BATTERY_REASON_JOIN_PERIODIC)
    {
      /* Reuse the already-settling conversion, but preserve the mandatory
         ten-real-failures checkpoint semantics. */
      battery_request = BATTERY_REASON_JOIN_FAILURES;
    }
    else
    {
      Battery_Start(BATTERY_REASON_JOIN_FAILURES, now);
    }
  }
  else
  {
    join_state = JOIN_STATE_WAIT_RESULT;
    join_deadline = now + RAK_JOIN_RESULT_TIMEOUT_MS;
  }
}

static void RAK_HandleJoin(uint32_t now, uint32_t events)
{
  if (AppLogic_JoinConfirmed(events))
  {
    App_StartActiveAfterJoin(now, "+EVT:JOINED");
    return;
  }

  if (((events & RAK_EVT_JOIN_FAILED) != 0U) &&
      ((join_state == JOIN_STATE_WAIT_ACCEPT) ||
       (join_state == JOIN_STATE_WAIT_RESULT) ||
       (join_state == JOIN_STATE_WAIT_STATUS)))
  {
    RAK_RecordJoinFailure(now, "JOIN_FAILED event");
    return;
  }

  if (((events & RAK_EVT_BUSY) != 0U) &&
      (join_state != JOIN_STATE_BATTERY_CHECK))
  {
    join_status_after_busy = true;
    join_state = JOIN_STATE_BUSY_BACKOFF;
    join_deadline = now + RAK_BUSY_BACKOFF_MS;
    Debug_Log("[RAK] AT_BUSY_ERROR; no radio failure counted, status query after backoff\r\n");
    return;
  }

  if (((events & RAK_EVT_PARAM_ERROR) != 0U) &&
      (join_state == JOIN_STATE_WAIT_ACCEPT))
  {
    join_state = JOIN_STATE_RETRY_WAIT;
    join_deadline = now + RAK_JOIN_RETRY_DELAY_MS;
    Debug_Log("[RAK] JOIN command parameter error; no radio failure counted\r\n");
    return;
  }

  if (((events & (RAK_EVT_ERROR | RAK_EVT_PARAM_ERROR)) != 0U) &&
      ((join_state == JOIN_STATE_WAIT_ACCEPT) ||
       (join_state == JOIN_STATE_WAIT_RESULT) ||
       (join_state == JOIN_STATE_WAIT_STATUS)))
  {
    if ((join_state == JOIN_STATE_WAIT_STATUS) && join_status_after_busy)
    {
      join_state = JOIN_STATE_RETRY_WAIT;
      join_deadline = now + RAK_JOIN_RETRY_DELAY_MS;
      Debug_Log("[RAK] status query after BUSY failed; no radio failure counted\r\n");
    }
    else
    {
      RAK_RecordJoinFailure(now, "AT/status error after a real JOIN attempt");
    }
    return;
  }

  switch (join_state)
  {
    case JOIN_STATE_IDLE:
      RAK_StartJoinAttempt(now);
      break;

    case JOIN_STATE_WAIT_ACCEPT:
      if ((events & RAK_EVT_OK) != 0U)
      {
        join_state = JOIN_STATE_WAIT_RESULT;
        join_deadline = now + RAK_JOIN_RESULT_TIMEOUT_MS;
        Debug_Log("[RAK] JOIN command OK only; waiting for final JOINED event\r\n");
      }
      else if (TimeReached(now, join_deadline))
      {
        App_RequestReconnect(now, "RAK silent after JOIN command");
      }
      break;

    case JOIN_STATE_WAIT_RESULT:
      if (TimeReached(now, join_deadline))
      {
        if (RAK_SendNetworkStatusCommand())
        {
          join_status_after_busy = false;
          join_state = JOIN_STATE_WAIT_STATUS;
          join_deadline = now + RAK_JOIN_STATUS_TIMEOUT_MS;
          Debug_Log("[RAK] final JOIN event timeout; checking AT+NJS\r\n");
        }
        else
        {
          App_RequestReconnect(now, "NJS query UART transmit failed");
        }
      }
      break;

    case JOIN_STATE_WAIT_STATUS:
      if ((events & RAK_EVT_NJS_1) != 0U)
      {
        /* NJS=1 proves that the modem is joined, but product startup is gated
           by the requested exact JOINED event.  Restart to obtain a fresh,
           observable JOIN sequence instead of silently entering ACTIVE. */
        App_RequestReconnect(now, "NJS=1 received without +EVT:JOINED");
      }
      else if ((events & RAK_EVT_NJS_0) != 0U)
      {
        join_state = JOIN_STATE_RETRY_WAIT;
        join_deadline = now + RAK_JOIN_RETRY_DELAY_MS;
        Debug_Log("[RAK] NJS=0; retry JOIN command without a total-attempt limit\r\n");
      }
      else if (TimeReached(now, join_deadline))
      {
        App_RequestReconnect(now, "RAK silent after AT+NJS query");
      }
      break;

    case JOIN_STATE_BUSY_BACKOFF:
      if (TimeReached(now, join_deadline))
      {
        if (RAK_SendNetworkStatusCommand())
        {
          join_state = JOIN_STATE_WAIT_STATUS;
          join_deadline = now + RAK_JOIN_STATUS_TIMEOUT_MS;
        }
        else
        {
          App_RequestReconnect(now, "NJS query UART transmit failed after BUSY");
        }
      }
      break;

    case JOIN_STATE_RETRY_WAIT:
      if (TimeReached(now, join_deadline))
      {
        RAK_StartJoinAttempt(now);
      }
      break;

    case JOIN_STATE_BATTERY_CHECK:
    default:
      break;
  }
}

static void RAK_CompleteUplinkFailure(uint32_t now, const char *reason)
{
  device_status_flags |= STATUS_SEND_ERROR;
  consecutive_send_failures++;
  LED_Indicate(false, now);
  Debug_Log("[RAK] uplink failed (%u consecutive): %s\r\n",
            (unsigned int)consecutive_send_failures, reason);

  App_RequestReconnect(now, reason);
}

static void RAK_HandleUplink(uint32_t now, uint32_t events)
{
  bool transaction_pending = (uplink_state == UPLINK_STATE_WAIT_ACCEPT) ||
                             (uplink_state == UPLINK_STATE_WAIT_RESULT);

  if ((events & (RAK_EVT_NO_NETWORK | RAK_EVT_NJS_0)) != 0U)
  {
    device_status_flags |= STATUS_JOIN_LOST;
    RAK_CompleteUplinkFailure(now, "network membership lost");
    return;
  }

  if ((events & RAK_EVT_SEND_OK) != 0U)
  {
    if (!transaction_pending && (uplink_state != UPLINK_STATE_RETRY_WAIT))
    {
      Debug_Log("[RAK] unsolicited final SEND success ignored\r\n");
      return;
    }
    uplink_state = UPLINK_STATE_IDLE;
    next_uplink_tick = now + UPLINK_PERIOD_MS;
    consecutive_send_failures = 0U;
    device_status_flags &= (uint8_t)~STATUS_SEND_ERROR;
    device_status_flags &= (uint8_t)~STATUS_JOIN_LOST;
    Debug_Log("[RAK] final uplink success event received; next in %lu ms\r\n",
              (uint32_t)UPLINK_PERIOD_MS);
    LED_Indicate(true, now);
    return;
  }

  if ((events & RAK_EVT_BUSY) != 0U)
  {
    if (uplink_state != UPLINK_STATE_WAIT_ACCEPT)
    {
      Debug_Log("[RAK] late/unsolicited BUSY ignored\r\n");
      return;
    }
    device_status_flags |= STATUS_SEND_ERROR;
    uplink_state = UPLINK_STATE_RETRY_WAIT;
    uplink_deadline = now + UPLINK_BUSY_RETRY_MS;
    Debug_Log("[RAK] uplink AT_BUSY_ERROR; retry after duty-cycle backoff\r\n");
    return;
  }

  if ((events & (RAK_EVT_SEND_FAILED | RAK_EVT_ERROR | RAK_EVT_PARAM_ERROR)) != 0U)
  {
    if (AppLogic_UplinkFailureIsCurrent(transaction_pending))
    {
      RAK_CompleteUplinkFailure(now, "SEND failure/AT error event");
    }
    else
    {
      Debug_Log("[RAK] late/unsolicited SEND failure ignored (already accounted)\r\n");
    }
    return;
  }

  switch (uplink_state)
  {
    case UPLINK_STATE_WAIT_ACCEPT:
      if ((events & RAK_EVT_OK) != 0U)
      {
        uplink_state = UPLINK_STATE_WAIT_RESULT;
        uplink_deadline = now + UPLINK_RESULT_TIMEOUT_MS;
        Debug_Log("[RAK] AT+SEND OK only; waiting for final SEND event\r\n");
      }
      else if (TimeReached(now, uplink_deadline))
      {
        RAK_CompleteUplinkFailure(now, "AT+SEND acceptance timeout");
      }
      break;

    case UPLINK_STATE_WAIT_RESULT:
      if (TimeReached(now, uplink_deadline))
      {
        RAK_CompleteUplinkFailure(now, "final SEND event timeout");
      }
      break;

    case UPLINK_STATE_RETRY_WAIT:
      if (TimeReached(now, uplink_deadline))
      {
        uplink_state = UPLINK_STATE_IDLE;
        next_uplink_tick = now;
      }
      break;

    case UPLINK_STATE_IDLE:
    default:
      break;
  }
}

static void RAK_BytesToHex(const uint8_t *bytes, uint16_t length, char *hex, uint16_t hex_size)
{
  static const char digits[] = "0123456789ABCDEF";
  uint16_t out = 0;

  for (uint16_t i = 0; (i < length) && ((out + 2U) < hex_size); i++)
  {
    uint8_t byte = bytes[i];
    hex[out++] = digits[(byte >> 4) & 0x0FU];
    hex[out++] = digits[byte & 0x0FU];
  }

  hex[out] = '\0';
}

static bool RAK_SendHexPayload(uint8_t port, const char *payload_hex)
{
  char cmd[RAK_SEND_CMD_SIZE] = {0};
  uint16_t pos = 0;

  TextAppend(cmd, sizeof(cmd), &pos, "AT+SEND=");
  TextAppendInt32(cmd, sizeof(cmd), &pos, port);
  TextAppend(cmd, sizeof(cmd), &pos, ":");
  TextAppend(cmd, sizeof(cmd), &pos, payload_hex);
  TextAppend(cmd, sizeof(cmd), &pos, "\r\n");

  Debug_Log("[RAK] port=%u payload_hex: %s\r\n", port, payload_hex);
  return RAK_SendCommand(cmd);
}

static bool RAK_SendBytesPayload(uint8_t port, const uint8_t *payload, uint16_t payload_size)
{
  char payload_hex[RAK_PAYLOAD_HEX_SIZE] = {0};

  RAK_BytesToHex(payload, payload_size, payload_hex, sizeof(payload_hex));
  return RAK_SendHexPayload(port, payload_hex);
}

static bool BuildMeasurementPayload(uint8_t *payload, uint16_t payload_size)
{
  const float values[] = {
    rms_voltage[0], rms_voltage[1], rms_voltage[2],
    rms_current[0], rms_current[1], rms_current[2],
    revolutions
  };

  if (payload_size < RAK_MEAS_PAYLOAD_SIZE)
  {
    return false;
  }

  payload[0] = 2U;
  for (uint8_t i = 0; i < 7U; i++)
  {
    float value = values[i];
    uint16_t scaled;

    if (value < 0.0f)
    {
      value = 0.0f;
    }
    if (value > 655.35f)
    {
      value = 655.35f;
    }

    scaled = (uint16_t)((value * 100.0f) + 0.5f);
    payload[1U + (i * 2U)] = (uint8_t)(scaled & 0xFFU);
    payload[2U + (i * 2U)] = (uint8_t)(scaled >> 8);
  }

  payload[15] = (uint8_t)(adc_x_raw & 0xFFU);
  payload[16] = (uint8_t)(adc_x_raw >> 8);
  payload[17] = (uint8_t)(adc_y_raw & 0xFFU);
  payload[18] = (uint8_t)(adc_y_raw >> 8);
  payload[19] = (uint8_t)(adc_z_raw & 0xFFU);
  payload[20] = (uint8_t)(adc_z_raw >> 8);

  {
    float value = battery_voltage;
    uint16_t scaled;

    if (value < 0.0f)
    {
      value = 0.0f;
    }
    if (value > 655.35f)
    {
      value = 655.35f;
    }
    scaled = (uint16_t)((value * 100.0f) + 0.5f);
    payload[21] = (uint8_t)(scaled & 0xFFU);
    payload[22] = (uint8_t)(scaled >> 8);
  }

  payload[23] = 0U;
  for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++)
  {
    int16_t temperature = 0;

    if ((i < ds18b20_count) && (ds18b20_temp_ok[i] == true))
    {
      temperature = (int16_t)ds18b20_temp_centi[i];
      payload[23] |= (uint8_t)(1U << i);
    }

    payload[24U + (i * 2U)] = (uint8_t)((uint16_t)temperature & 0xFFU);
    payload[25U + (i * 2U)] = (uint8_t)(((uint16_t)temperature) >> 8);
  }

  payload[32] = device_status_flags;

  return true;
}

static void DS18B20_LineConfigureActive(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  HAL_GPIO_WritePin(DS_GPIO_Port, DS_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = DS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DS_GPIO_Port, &GPIO_InitStruct);
}

static void DS18B20_LineSafeOff(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = DS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DS_GPIO_Port, &GPIO_InitStruct);
}

static void DS18B20_LineRelease(void)
{
  HAL_GPIO_WritePin(DS_GPIO_Port, DS_Pin, GPIO_PIN_SET);
}

static void DS18B20_LineLow(void)
{
  HAL_GPIO_WritePin(DS_GPIO_Port, DS_Pin, GPIO_PIN_RESET);
}

static uint8_t DS18B20_LineRead(void)
{
  return (HAL_GPIO_ReadPin(DS_GPIO_Port, DS_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t DS18B20_Reset(void)
{
  uint8_t presence;
  uint32_t primask;

  DS18B20_LineLow();
  DWT_DelayUs(480);
  primask = Critical_Enter();
  DS18B20_LineRelease();
  DWT_DelayUs(70);
  presence = (DS18B20_LineRead() == 0U) ? 1U : 0U;
  Critical_Exit(primask);
  DWT_DelayUs(410);

  return presence;
}

static void DS18B20_WriteBit(uint8_t bit)
{
  uint32_t primask = Critical_Enter();

  DS18B20_LineLow();
  if (bit != 0U)
  {
    DWT_DelayUs(6);
    DS18B20_LineRelease();
    Critical_Exit(primask);
    DWT_DelayUs(64);
  }
  else
  {
    DWT_DelayUs(60);
    DS18B20_LineRelease();
    Critical_Exit(primask);
    DWT_DelayUs(10);
  }
}

static uint8_t DS18B20_ReadBit(void)
{
  uint8_t bit;
  uint32_t primask = Critical_Enter();

  DS18B20_LineLow();
  DWT_DelayUs(6);
  DS18B20_LineRelease();
  DWT_DelayUs(9);
  bit = DS18B20_LineRead();
  Critical_Exit(primask);
  DWT_DelayUs(55);

  return bit;
}

static void DS18B20_WriteByte(uint8_t byte)
{
  for (uint8_t i = 0; i < 8U; i++)
  {
    DS18B20_WriteBit(byte & 0x01U);
    byte >>= 1;
  }
}

static uint8_t DS18B20_ReadByte(void)
{
  uint8_t byte = 0;

  for (uint8_t i = 0; i < 8U; i++)
  {
    byte >>= 1;
    if (DS18B20_ReadBit() != 0U)
    {
      byte |= 0x80U;
    }
  }

  return byte;
}

static uint8_t DS18B20_Crc8(const uint8_t *data, uint8_t len)
{
  return AppLogic_DallasCrc8(data, len);
}

static uint8_t DS18B20_Search(uint8_t roms[][8], uint8_t max_devices)
{
  uint8_t found = 0;
  uint8_t last_rom[8] = {0};
  uint8_t last_discrepancy = 0;
  uint8_t last_device = 0;

  while ((last_device == 0U) && (found < max_devices))
  {
    uint8_t rom[8] = {0};
    uint8_t discrepancy_marker = 0;
    uint8_t rom_byte = 0;
    uint8_t rom_mask = 1;

    if (DS18B20_Reset() == 0U)
    {
      Debug_Log("[DS18B20] search stopped: no presence pulse\r\n");
      break;
    }

    DS18B20_WriteByte(0xF0U);

    for (uint8_t bit_number = 1; bit_number <= 64U; bit_number++)
    {
      uint8_t bit = DS18B20_ReadBit();
      uint8_t complement = DS18B20_ReadBit();
      uint8_t chosen_bit;

      if ((bit == 1U) && (complement == 1U))
      {
        Debug_Log("[DS18B20] search bus fault at bit %u\r\n", bit_number);
        return found;
      }

      if (bit != complement)
      {
        chosen_bit = bit;
      }
      else
      {
        if (bit_number < last_discrepancy)
        {
          chosen_bit = ((last_rom[rom_byte] & rom_mask) != 0U) ? 1U : 0U;
        }
        else
        {
          chosen_bit = (bit_number == last_discrepancy) ? 1U : 0U;
        }

        if (chosen_bit == 0U)
        {
          discrepancy_marker = bit_number;
        }
      }

      if (chosen_bit != 0U)
      {
        rom[rom_byte] |= rom_mask;
      }

      DS18B20_WriteBit(chosen_bit);

      rom_mask = (uint8_t)(rom_mask << 1);
      if (rom_mask == 0U)
      {
        rom_byte++;
        rom_mask = 1;
      }
    }

    if ((rom[0] == 0x28U) && (DS18B20_Crc8(rom, 7) == rom[7]))
    {
      memcpy(roms[found], rom, 8);
      memcpy(last_rom, rom, 8);
      Debug_Log("[DS18B20] found ROM %u: %02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                (unsigned int)(found + 1U),
                rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
      found++;
    }
    else
    {
      Debug_Log("[DS18B20] invalid ROM or CRC: %02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
      break;
    }

    last_discrepancy = discrepancy_marker;
    if (last_discrepancy == 0U)
    {
      last_device = 1U;
    }
  }

  return found;
}

static bool DS18B20_ReadScratchpad(const uint8_t rom[8], uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE])
{
  if (DS18B20_Reset() == 0U)
  {
    Debug_Log("[DS18B20] scratchpad read failed: no presence pulse\r\n");
    return false;
  }

  DS18B20_WriteByte(0x55U);
  for (uint8_t i = 0; i < 8U; i++)
  {
    DS18B20_WriteByte(rom[i]);
  }

  DS18B20_WriteByte(0xBEU);
  for (uint8_t i = 0; i < DS18B20_SCRATCHPAD_SIZE; i++)
  {
    scratchpad[i] = DS18B20_ReadByte();
  }

  if (DS18B20_Crc8(scratchpad, 8) != scratchpad[8])
  {
    Debug_Log("[DS18B20] scratchpad CRC failed: %02X %02X %02X %02X %02X %02X %02X %02X crc=%02X\r\n",
              scratchpad[0], scratchpad[1], scratchpad[2], scratchpad[3], scratchpad[4],
              scratchpad[5], scratchpad[6], scratchpad[7], scratchpad[8]);
    return false;
  }

  return true;
}

static bool DS18B20_ReadTemperatureCenti(const uint8_t rom[8], int32_t *temp_centi)
{
  uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
  int16_t raw;

  if (DS18B20_ReadScratchpad(rom, scratchpad) == false)
  {
    return false;
  }

  raw = (int16_t)((uint16_t)scratchpad[0] | ((uint16_t)scratchpad[1] << 8));
  *temp_centi = ((int32_t)raw * 100) / 16;

  return true;
}

static void DS18B20_DiscoverCached(uint32_t now)
{
  uint8_t discovered[DS18B20_MAX_SENSORS][8] = {0};
  uint8_t discovered_count;
  uint8_t previous_count = ds18b20_count;

  if (ds18b20_count >= DS18B20_MAX_SENSORS)
  {
    return;
  }

  Debug_Log("[DS18B20] ROM search; %u cached slot(s) remain fixed\r\n",
            (unsigned int)ds18b20_count);
  discovered_count = DS18B20_Search(discovered, DS18B20_MAX_SENSORS);

  for (uint8_t candidate = 0U;
       (candidate < discovered_count) && (ds18b20_count < DS18B20_MAX_SENSORS);
       candidate++)
  {
    bool already_cached = false;

    for (uint8_t cached = 0U; cached < ds18b20_count; cached++)
    {
      if (memcmp(ds18b20_roms[cached], discovered[candidate], 8U) == 0)
      {
        already_cached = true;
        break;
      }
    }

    if (!already_cached)
    {
      memcpy(ds18b20_roms[ds18b20_count], discovered[candidate], 8U);
      ds18b20_temp_centi[ds18b20_count] = 0;
      ds18b20_temp_ok[ds18b20_count] = false;
      ds18b20_count++;
    }
  }

  if (ds18b20_count == 0U)
  {
    device_status_flags |= STATUS_DS18B20_ERROR;
    ds18b20_discovery_tick = now + DS18B20_EMPTY_RETRY_MS;
    Debug_Log("[DS18B20] no sensors found; retry in %lu ms without remapping slots\r\n",
              (uint32_t)DS18B20_EMPTY_RETRY_MS);
  }
  else
  {
    ds18b20_discovery_tick = now + DS18B20_PARTIAL_RETRY_MS;
    if (ds18b20_count != previous_count)
    {
      Debug_Log("[DS18B20] cached %u ROM slot(s); existing slot order preserved\r\n",
                ds18b20_count);
    }
  }
}

static bool DS18B20_StartConversion(void)
{
  if ((ds18b20_count == 0U) || (DS18B20_Reset() == 0U))
  {
    return false;
  }

  DS18B20_WriteByte(0xCCU);
  DS18B20_WriteByte(0x44U);
  return true;
}

static void DS18B20_Service(uint32_t now)
{
  if ((ds18b20_state == DS18B20_STATE_IDLE) &&
      (ds18b20_count < DS18B20_MAX_SENSORS) &&
      TimeReached(now, ds18b20_discovery_tick))
  {
    DS18B20_DiscoverCached(now);
  }
  if (ds18b20_count == 0U)
  {
    return;
  }

  if ((ds18b20_state == DS18B20_STATE_IDLE) && TimeReached(now, ds18b20_next_tick))
  {
    if (DS18B20_StartConversion())
    {
      ds18b20_state = DS18B20_STATE_CONVERTING;
      ds18b20_deadline = now + DS18B20_CONVERT_MS;
      Debug_Log("[DS18B20] conversion started asynchronously\r\n");
    }
    else
    {
      device_status_flags |= STATUS_DS18B20_ERROR;
      ds18b20_next_tick = now + BATTERY_ERROR_RETRY_MS;
      Debug_Log("[DS18B20] conversion start failed\r\n");
    }
    return;
  }

  if ((ds18b20_state == DS18B20_STATE_CONVERTING) && TimeReached(now, ds18b20_deadline))
  {
    bool all_ok = true;

    for (uint8_t i = 0; i < ds18b20_count; i++)
    {
      ds18b20_temp_ok[i] = DS18B20_ReadTemperatureCenti(ds18b20_roms[i],
                                                        &ds18b20_temp_centi[i]);
      if (ds18b20_temp_ok[i])
      {
        Debug_Log("[DS18B20] sensor %u temp_x100=%ld\r\n",
                  (unsigned int)(i + 1U), ds18b20_temp_centi[i]);
      }
      else
      {
        all_ok = false;
        Debug_Log("[DS18B20] sensor %u read/CRC failed\r\n",
                  (unsigned int)(i + 1U));
      }
    }

    if (all_ok)
    {
      device_status_flags &= (uint8_t)~STATUS_DS18B20_ERROR;
    }
    else
    {
      device_status_flags |= STATUS_DS18B20_ERROR;
    }

    ds18b20_state = DS18B20_STATE_IDLE;
    ds18b20_next_tick = now + DS18B20_PERIOD_MS;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  App_AssertPowerHoldEarly();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  Debug_Log("\r\n[BOOT] wind start\r\n");
  Debug_Log("[BOOT] SystemClock configured before GPIO/UART init: SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu\r\n",
            HAL_RCC_GetSysClockFreq(), HAL_RCC_GetHCLKFreq(),
            HAL_RCC_GetPCLK1Freq(), HAL_RCC_GetPCLK2Freq());
  Debug_Log("[BOOT] GPIO, DMA, ADC1, ADC2, USART1, USART2, TIM2 init done\r\n");
  DWT_Delay_Init();
  Debug_Log("[BOOT] DWT delay init done\r\n");
  App_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_Run();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 6;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = 3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = 4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = 5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = 6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.ScanConvMode = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 8399;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 3;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */
  HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */
  DEBUG_USART2_GPIO_Clock_Init();
  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  debug_uart_ready = true;
  Debug_Log("[DBG] USART2 ready on PD5 TX / PD6 RX, 115200 8N1\r\n");
  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ON_AX_GPIO_Port, ON_AX_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ON_FR_GPIO_Port, ON_FR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10|GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, ON_RAK_Pin|ON_AN_Pin|ON_PWR_DET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : ON_AX_Pin */
  GPIO_InitStruct.Pin = ON_AX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ON_AX_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Freqency_Pin */
  GPIO_InitStruct.Pin = Freqency_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Freqency_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ON_FR_Pin */
  GPIO_InitStruct.Pin = ON_FR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ON_FR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PD10 PD14 PWR_OFF_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_14|PWR_OFF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : ON_RAK_Pin ON_AN_Pin ON_PWR_DET_Pin */
  GPIO_InitStruct.Pin = ON_RAK_Pin|ON_AN_Pin|ON_PWR_DET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : DS_Pin */
  GPIO_InitStruct.Pin = DS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PWR_Pin */
  GPIO_InitStruct.Pin = PWR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PWR_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_GPIO_WritePin(DS_GPIO_Port, DS_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = DS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DS_GPIO_Port, &GPIO_InitStruct);

  /* RAK is the only external device powered during startup.  Keep the
     unpowered 1-Wire branch high-impedance until measurement power is valid. */
  DS18B20_LineSafeOff();
  HAL_GPIO_WritePin(ON_RAK_GPIO_Port, ON_RAK_Pin, GPIO_PIN_SET);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static void RAK_RecordRxFaultFromIRQ(void)
{
  if (rak_rx_fault_pending == 0U)
  {
    rak_rx_fault_boundary = rak_rx_head;
    rak_rx_fault_pending = 1U;
  }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    adc_dma_generation++;
    if ((adc_dma_pending_mask & 0x01U) != 0U)
    {
      adc_dma_overrun = 1U;
    }
    adc_dma_pending_mask |= 0x01U;
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    adc_dma_generation++;
    if ((adc_dma_pending_mask & 0x02U) != 0U)
    {
      adc_dma_overrun = 1U;
    }
    adc_dma_pending_mask |= 0x02U;
  }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    adc_dma_generation++;
    adc_dma_error = 1U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uint16_t next = AppLogic_RingNext(rak_rx_head, RAK_RX_RING_SIZE - 1U);

    if (next == rak_rx_tail)
    {
      RAK_RecordRxFaultFromIRQ();
      rak_rx_overflow = 1U;
    }
    else
    {
      rak_rx_ring[rak_rx_head] = rak_rx_byte;
      __DMB();
      rak_rx_head = next;
    }

    if (rak_uart_rx_enabled)
    {
      if (HAL_UART_Receive_IT(&RAK_UART, &rak_rx_byte, 1U) != HAL_OK)
      {
        RAK_RecordRxFaultFromIRQ();
        rak_rx_error = 1U;
      }
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    RAK_RecordRxFaultFromIRQ();
    rak_rx_error = 1U;
    if (rak_uart_rx_enabled)
    {
      __HAL_UART_CLEAR_OREFLAG(&RAK_UART);
      if (HAL_UART_Receive_IT(&RAK_UART, &rak_rx_byte, 1U) != HAL_OK)
      {
        rak_rx_error = 1U;
      }
    }
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if ((GPIO_Pin == Freqency_Pin) && rotation_count_enabled)
  {
    frequency_pulse_count++;
    rotation_check_pulse_count++;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __enable_irq();
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U)
  {
    Debug_Log("[FAULT] critical HAL initialization error; controlled MCU reset\r\n");
  }
  for (volatile uint32_t reset_guard = 0U; reset_guard < 720000U; reset_guard++)
  {
    __NOP();
  }
  NVIC_SystemReset();
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
