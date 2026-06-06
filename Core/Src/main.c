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
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_CHANNELS            6U
#define ADC_MAX                 4095.0f
#define VREF                    3.0f
#define ANALOG_MIDPOINT_V       1.5f
#define RMS_SAMPLES             200U

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

#define RAK_UART                huart1
#define RAK_RX_BUFFER_SIZE      192U
#define RAK_PAYLOAD_TEXT_SIZE   160U
#define RAK_PAYLOAD_HEX_SIZE    ((RAK_PAYLOAD_TEXT_SIZE * 2U) + 1U)
#define RAK_SEND_CMD_SIZE       (24U + RAK_PAYLOAD_HEX_SIZE)
#define RAK_JOIN_TIMEOUT_MS     10000U
#define RAK_AT_TIMEOUT_MS       5000U
#define RAK_BOOT_DELAY_MS       1500U

#define DEBUG_UART              huart2
#define DEBUG_LOG_BUFFER_SIZE   192U

#define DS18B20_MAX_SENSORS     4U
#define DS18B20_SCRATCHPAD_SIZE 9U
#define DS18B20_CONVERT_MS      750U

#define SHUTDOWN_DELAY_MS       200U

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
static uint16_t adc_buffer[ADC_CHANNELS] = {0};
static float sum[ADC_CHANNELS] = {0};
static float sum_sq[ADC_CHANNELS] = {0};
static float rms_voltage[3] = {0};
static float rms_current[3] = {0};
static volatile uint32_t rms_count = 0;
static volatile uint8_t rms_ready = 0;

static volatile uint32_t frequency_pulse_count = 0;
static volatile uint32_t frequency_pulses_ready = 0;
static float revolutions = 0.0f;

static uint16_t adc_x_raw = 0;
static uint16_t adc_y_raw = 0;
static uint16_t adc_z_raw = 0;
static uint16_t adc_bat_raw = 0;
static float battery_voltage = 0.0f;

static uint8_t ds18b20_roms[DS18B20_MAX_SENSORS][8] = {0};
static uint8_t ds18b20_count = 0;
static int32_t ds18b20_temp_centi[DS18B20_MAX_SENSORS] = {0};
static bool ds18b20_temp_ok[DS18B20_MAX_SENSORS] = {0};

static uint8_t rak_rx_byte = 0;
static char rak_rx_buffer[RAK_RX_BUFFER_SIZE] = {0};
static bool debug_uart_ready = false;

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
static void App_StartPowerSequence(void);
static void App_StartMeasurements(void);
static void App_ProcessMeasurementWindow(void);
static void App_CheckPowerAndShutdownIfNeeded(void);
static void App_SafeShutdown(void);

static void LED_Blink(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, uint8_t count, uint32_t on_ms, uint32_t off_ms);
static void LED_SendSuccessBlink(void);
static void DWT_Delay_Init(void);
static void DWT_DelayUs(uint32_t us);

static uint16_t ADC2_ReadChannel(uint32_t channel);
static void ReadAuxAnalogInputs(void);
static float RawToVoltage(uint16_t raw);
static float BatteryVoltageFromRaw(uint16_t raw);

static void TextAppend(char *dst, uint16_t dst_size, uint16_t *pos, const char *src);
static void TextAppendInt32(char *dst, uint16_t dst_size, uint16_t *pos, int32_t value);
static void TextAppendFixed2(char *dst, uint16_t dst_size, uint16_t *pos, float value);
static void TextAppendRaw(char *dst, uint16_t dst_size, uint16_t *pos, const char *name, uint16_t raw);

static void RAK_ClearOldUartData(void);
static bool RAK_WaitForJoin(uint32_t timeout_ms);
static bool RAK_SendCommandAndWaitOK(const char *cmd, uint32_t timeout_ms);
static void RAK_AsciiToHex(const char *ascii, char *hex, uint16_t hex_size);
static bool RAK_SendAsciiPayload(uint8_t port, const char *payload_text);
static bool BuildPayloadPart1(char *payload, uint16_t payload_size);
static bool BuildPayloadPart2(char *payload, uint16_t payload_size);

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
static void DS18B20_ReadAllTemperatures(void);

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

  (void)HAL_UART_Transmit(&DEBUG_UART, (uint8_t *)buffer, (uint16_t)len, 200);
}

static void App_StartPowerSequence(void)
{
  Debug_Log("[APP] power sequence start\r\n");
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ON_PWR_DET_GPIO_Port, ON_PWR_DET_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ON_RAK_GPIO_Port, ON_RAK_Pin, GPIO_PIN_SET);

  Debug_Log("[APP] RAK power on, wait %lu ms\r\n", (uint32_t)RAK_BOOT_DELAY_MS);
  HAL_Delay(RAK_BOOT_DELAY_MS);

  Debug_Log("[RAK] wait join, timeout=%lu ms\r\n", (uint32_t)RAK_JOIN_TIMEOUT_MS);
  if (RAK_WaitForJoin(RAK_JOIN_TIMEOUT_MS) == true)
  {
    Debug_Log("[RAK] joined\r\n");
    LED_Blink(GPIOD, GPIO_PIN_10, 1, 200, 100);
  }
  else
  {
    Debug_Log("[RAK] join timeout, RX: %s\r\n", rak_rx_buffer);
    LED_Blink(GPIOD, GPIO_PIN_14, 1, 200, 100);
  }

  HAL_GPIO_WritePin(ON_AN_GPIO_Port, ON_AN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ON_FR_GPIO_Port, ON_FR_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ON_AX_GPIO_Port, ON_AX_Pin, GPIO_PIN_SET);
  Debug_Log("[APP] analog/frequency/aux power enabled\r\n");
}

static void App_StartMeasurements(void)
{
  Debug_Log("[MEAS] start: ADC Vref=%ld mV, midpoint=%ld mV, samples=%u\r\n",
            (int32_t)(VREF * 1000.0f),
            (int32_t)(ANALOG_MIDPOINT_V * 1000.0f),
            (unsigned int)RMS_SAMPLES);

  memset(adc_buffer, 0, sizeof(adc_buffer));
  memset(sum, 0, sizeof(sum));
  memset(sum_sq, 0, sizeof(sum_sq));
  rms_count = 0;
  rms_ready = 0;
  frequency_pulse_count = 0;
  frequency_pulses_ready = 0;

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, ADC_CHANNELS) != HAL_OK)
  {
    Debug_Log("[MEAS] HAL_ADC_Start_DMA failed\r\n");
    Error_Handler();
  }

  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
  {
    Debug_Log("[MEAS] HAL_TIM_Base_Start_IT failed\r\n");
    Error_Handler();
  }

  Debug_Log("[MEAS] ADC DMA and TIM2 started\r\n");
}

static void App_ProcessMeasurementWindow(void)
{
  char payload[RAK_PAYLOAD_TEXT_SIZE] = {0};
  float local_sum[ADC_CHANNELS];
  float local_sum_sq[ADC_CHANNELS];
  uint32_t pulses;
  bool send_ok;

  Debug_Log("[MEAS] window ready\r\n");

  __disable_irq();
  for (uint8_t i = 0; i < ADC_CHANNELS; i++)
  {
    local_sum[i] = sum[i];
    local_sum_sq[i] = sum_sq[i];
    sum[i] = 0.0f;
    sum_sq[i] = 0.0f;
  }
  pulses = frequency_pulses_ready;
  frequency_pulses_ready = 0;
  rms_ready = 0;
  __enable_irq();

  for (uint8_t i = 0; i < 3U; i++)
  {
    float mean_v = local_sum[i] / (float)RMS_SAMPLES;
    float var_v = (local_sum_sq[i] / (float)RMS_SAMPLES) - (mean_v * mean_v);
    float mean_i = local_sum[i + 3U] / (float)RMS_SAMPLES;
    float var_i = (local_sum_sq[i + 3U] / (float)RMS_SAMPLES) - (mean_i * mean_i);
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

  revolutions = (float)pulses / (float)PULSES_PER_REV;
  Debug_Log("[MEAS] pulses=%lu, revolutions_x100=%ld\r\n",
            pulses, (int32_t)(revolutions * 100.0f));

  ReadAuxAnalogInputs();
  DS18B20_ReadAllTemperatures();

  send_ok = BuildPayloadPart1(payload, sizeof(payload));
  Debug_Log("[PAYLOAD] part1: %s\r\n", payload);
  send_ok = RAK_SendAsciiPayload(2, payload) && send_ok;

  if (BuildPayloadPart2(payload, sizeof(payload)) == true)
  {
    Debug_Log("[PAYLOAD] part2: %s\r\n", payload);
    send_ok = RAK_SendAsciiPayload(3, payload) && send_ok;
  }
  else
  {
    Debug_Log("[PAYLOAD] part2 build failed\r\n");
    send_ok = false;
  }

  if (send_ok == true)
  {
    Debug_Log("[APP] send cycle OK\r\n");
    LED_SendSuccessBlink();
  }
  else
  {
    Debug_Log("[APP] send cycle failed\r\n");
    LED_Blink(GPIOD, GPIO_PIN_14, 2, 120, 120);
  }

  App_CheckPowerAndShutdownIfNeeded();
}

static void App_CheckPowerAndShutdownIfNeeded(void)
{
  if (HAL_GPIO_ReadPin(PWR_GPIO_Port, PWR_Pin) == GPIO_PIN_RESET)
  {
    adc_bat_raw = ADC2_ReadChannel(ADC_CHANNEL_3);
    battery_voltage = BatteryVoltageFromRaw(adc_bat_raw);
    Debug_Log("[PWR] input low, battery=%ld.%02ld V raw=%u\r\n",
              (int32_t)battery_voltage,
              (int32_t)((battery_voltage - (float)((int32_t)battery_voltage)) * 100.0f),
              adc_bat_raw);

    if (battery_voltage < BAT_LOW_THRESHOLD_V)
    {
      Debug_Log("[PWR] battery below threshold, shutdown\r\n");
      App_SafeShutdown();
    }
  }
}

static void App_SafeShutdown(void)
{
  Debug_Log("[APP] safe shutdown start\r\n");
  HAL_TIM_Base_Stop_IT(&htim2);
  HAL_ADC_Stop_DMA(&hadc1);
  HAL_ADC_Stop(&hadc2);

  HAL_GPIO_WritePin(ON_RAK_GPIO_Port, ON_RAK_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_FR_GPIO_Port, ON_FR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_AN_GPIO_Port, ON_AN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_PWR_DET_GPIO_Port, ON_PWR_DET_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ON_AX_GPIO_Port, ON_AX_Pin, GPIO_PIN_RESET);

  HAL_Delay(SHUTDOWN_DELAY_MS);
  HAL_GPIO_WritePin(PWR_OFF_GPIO_Port, PWR_OFF_Pin, GPIO_PIN_RESET);

  while (1)
  {
  }
}

static void LED_Blink(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
  for (uint8_t i = 0; i < count; i++)
  {
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, GPIO_PIN_SET);
    HAL_Delay(on_ms);
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, GPIO_PIN_RESET);
    HAL_Delay(off_ms);
  }
}

static void LED_SendSuccessBlink(void)
{
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_SET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_RESET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_SET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_RESET);
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

static uint16_t ADC2_ReadChannel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  uint16_t value = 0;

  HAL_ADC_Stop(&hadc2);

  sConfig.Channel = channel;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;

  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Debug_Log("[ADC2] config failed channel=%lu\r\n", channel);
    return 0;
  }

  if (HAL_ADC_Start(&hadc2) == HAL_OK)
  {
    if (HAL_ADC_PollForConversion(&hadc2, 20) == HAL_OK)
    {
      value = (uint16_t)HAL_ADC_GetValue(&hadc2);
    }
    else
    {
      Debug_Log("[ADC2] poll timeout channel=%lu\r\n", channel);
    }
  }
  else
  {
    Debug_Log("[ADC2] start failed channel=%lu\r\n", channel);
  }
  HAL_ADC_Stop(&hadc2);

  return value;
}

static void ReadAuxAnalogInputs(void)
{
  adc_z_raw = ADC2_ReadChannel(ADC_CHANNEL_0);
  adc_y_raw = ADC2_ReadChannel(ADC_CHANNEL_1);
  adc_x_raw = ADC2_ReadChannel(ADC_CHANNEL_2);
  adc_bat_raw = ADC2_ReadChannel(ADC_CHANNEL_3);
  battery_voltage = BatteryVoltageFromRaw(adc_bat_raw);

  Debug_Log("[ADC2] aux raw: X=%u Y=%u Z=%u BAT=%u BAT_x100=%ld\r\n",
            adc_x_raw, adc_y_raw, adc_z_raw, adc_bat_raw,
            (int32_t)(battery_voltage * 100.0f));
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

static void TextAppendFixed2(char *dst, uint16_t dst_size, uint16_t *pos, float value)
{
  int32_t scaled = (int32_t)((value * 100.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
  int32_t abs_scaled = scaled;
  int32_t whole;
  int32_t frac;

  if (abs_scaled < 0)
  {
    TextAppend(dst, dst_size, pos, "-");
    abs_scaled = -abs_scaled;
  }

  whole = abs_scaled / 100;
  frac = abs_scaled % 100;
  TextAppendInt32(dst, dst_size, pos, whole);
  TextAppend(dst, dst_size, pos, ".");
  if (frac < 10)
  {
    TextAppend(dst, dst_size, pos, "0");
  }
  TextAppendInt32(dst, dst_size, pos, frac);
}

static void TextAppendRaw(char *dst, uint16_t dst_size, uint16_t *pos, const char *name, uint16_t raw)
{
  TextAppend(dst, dst_size, pos, name);
  TextAppend(dst, dst_size, pos, "=");
  TextAppendInt32(dst, dst_size, pos, raw);
}

static void RAK_ClearOldUartData(void)
{
  uint16_t cleared = 0;

  while (HAL_UART_Receive(&RAK_UART, &rak_rx_byte, 1, 5) == HAL_OK)
  {
    cleared++;
  }

  if (cleared > 0U)
  {
    Debug_Log("[RAK] cleared old UART bytes: %u\r\n", cleared);
  }
}

static bool RAK_WaitForJoin(uint32_t timeout_ms)
{
  uint16_t rx_index = 0;
  uint32_t start_tick = HAL_GetTick();

  memset(rak_rx_buffer, 0, sizeof(rak_rx_buffer));

  while ((HAL_GetTick() - start_tick) < timeout_ms)
  {
    if (HAL_UART_Receive(&RAK_UART, &rak_rx_byte, 1, 50) == HAL_OK)
    {
      if (rx_index < (RAK_RX_BUFFER_SIZE - 1U))
      {
        rak_rx_buffer[rx_index++] = (char)rak_rx_byte;
        rak_rx_buffer[rx_index] = '\0';
      }

      if (strstr(rak_rx_buffer, "+EVT:JOINED") != NULL)
      {
        Debug_Log("[RAK] join event RX: %s\r\n", rak_rx_buffer);
        return true;
      }
    }
  }

  return false;
}

static bool RAK_SendCommandAndWaitOK(const char *cmd, uint32_t timeout_ms)
{
  uint16_t rx_index = 0;
  uint32_t start_tick;

  memset(rak_rx_buffer, 0, sizeof(rak_rx_buffer));
  RAK_ClearOldUartData();

  Debug_Log("[RAK] TX: %s", cmd);
  if (HAL_UART_Transmit(&RAK_UART, (uint8_t *)cmd, strlen(cmd), 1000) != HAL_OK)
  {
    Debug_Log("[RAK] TX failed, err=0x%08lX\r\n", HAL_UART_GetError(&RAK_UART));
    return false;
  }

  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < timeout_ms)
  {
    if (HAL_UART_Receive(&RAK_UART, &rak_rx_byte, 1, 20) == HAL_OK)
    {
      if (rx_index < (RAK_RX_BUFFER_SIZE - 1U))
      {
        rak_rx_buffer[rx_index++] = (char)rak_rx_byte;
        rak_rx_buffer[rx_index] = '\0';
      }

      if (strstr(rak_rx_buffer, "OK") != NULL)
      {
        Debug_Log("[RAK] RX OK: %s\r\n", rak_rx_buffer);
        return true;
      }
      if ((strstr(rak_rx_buffer, "ERROR") != NULL) || (strstr(rak_rx_buffer, "AT_") != NULL))
      {
        Debug_Log("[RAK] RX error: %s\r\n", rak_rx_buffer);
        return false;
      }
    }
  }

  Debug_Log("[RAK] timeout %lu ms, RX: %s\r\n", timeout_ms, rak_rx_buffer);
  return false;
}

static void RAK_AsciiToHex(const char *ascii, char *hex, uint16_t hex_size)
{
  static const char digits[] = "0123456789ABCDEF";
  uint16_t out = 0;

  while ((*ascii != '\0') && ((out + 2U) < hex_size))
  {
    uint8_t byte = (uint8_t)*ascii;
    hex[out++] = digits[(byte >> 4) & 0x0FU];
    hex[out++] = digits[byte & 0x0FU];
    ascii++;
  }

  hex[out] = '\0';
}

static bool RAK_SendAsciiPayload(uint8_t port, const char *payload_text)
{
  char payload_hex[RAK_PAYLOAD_HEX_SIZE] = {0};
  char cmd[RAK_SEND_CMD_SIZE] = {0};
  uint16_t pos = 0;

  RAK_AsciiToHex(payload_text, payload_hex, sizeof(payload_hex));

  TextAppend(cmd, sizeof(cmd), &pos, "AT+SEND=");
  TextAppendInt32(cmd, sizeof(cmd), &pos, port);
  TextAppend(cmd, sizeof(cmd), &pos, ":");
  TextAppend(cmd, sizeof(cmd), &pos, payload_hex);
  TextAppend(cmd, sizeof(cmd), &pos, "\r\n");

  Debug_Log("[RAK] port=%u payload_hex: %s\r\n", port, payload_hex);
  return RAK_SendCommandAndWaitOK(cmd, RAK_AT_TIMEOUT_MS);
}

static bool BuildPayloadPart1(char *payload, uint16_t payload_size)
{
  uint16_t pos = 0;

  payload[0] = '\0';
  TextAppend(payload, payload_size, &pos, "VA=");
  TextAppendFixed2(payload, payload_size, &pos, rms_voltage[0]);
  TextAppend(payload, payload_size, &pos, ";VB=");
  TextAppendFixed2(payload, payload_size, &pos, rms_voltage[1]);
  TextAppend(payload, payload_size, &pos, ";VC=");
  TextAppendFixed2(payload, payload_size, &pos, rms_voltage[2]);
  TextAppend(payload, payload_size, &pos, ";IA=");
  TextAppendFixed2(payload, payload_size, &pos, rms_current[0]);
  TextAppend(payload, payload_size, &pos, ";IB=");
  TextAppendFixed2(payload, payload_size, &pos, rms_current[1]);
  TextAppend(payload, payload_size, &pos, ";IC=");
  TextAppendFixed2(payload, payload_size, &pos, rms_current[2]);
  TextAppend(payload, payload_size, &pos, ";REV=");
  TextAppendFixed2(payload, payload_size, &pos, revolutions);

  return true;
}

static bool BuildPayloadPart2(char *payload, uint16_t payload_size)
{
  uint16_t pos = 0;

  payload[0] = '\0';
  TextAppendRaw(payload, payload_size, &pos, "X", adc_x_raw);
  TextAppend(payload, payload_size, &pos, ";");
  TextAppendRaw(payload, payload_size, &pos, "Y", adc_y_raw);
  TextAppend(payload, payload_size, &pos, ";");
  TextAppendRaw(payload, payload_size, &pos, "Z", adc_z_raw);
  TextAppend(payload, payload_size, &pos, ";BAT=");
  TextAppendFixed2(payload, payload_size, &pos, battery_voltage);
  TextAppend(payload, payload_size, &pos, ";T=");

  for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++)
  {
    if (i > 0U)
    {
      TextAppend(payload, payload_size, &pos, ",");
    }

    if ((i < ds18b20_count) && (ds18b20_temp_ok[i] == true))
    {
      TextAppendFixed2(payload, payload_size, &pos, ((float)ds18b20_temp_centi[i]) / 100.0f);
    }
    else
    {
      TextAppend(payload, payload_size, &pos, "ERR");
    }
  }

  return true;
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

  DS18B20_LineLow();
  DWT_DelayUs(480);
  DS18B20_LineRelease();
  DWT_DelayUs(70);
  presence = (DS18B20_LineRead() == 0U) ? 1U : 0U;
  DWT_DelayUs(410);

  return presence;
}

static void DS18B20_WriteBit(uint8_t bit)
{
  DS18B20_LineLow();

  if (bit != 0U)
  {
    DWT_DelayUs(6);
    DS18B20_LineRelease();
    DWT_DelayUs(64);
  }
  else
  {
    DWT_DelayUs(60);
    DS18B20_LineRelease();
    DWT_DelayUs(10);
  }
}

static uint8_t DS18B20_ReadBit(void)
{
  uint8_t bit;

  DS18B20_LineLow();
  DWT_DelayUs(6);
  DS18B20_LineRelease();
  DWT_DelayUs(9);
  bit = DS18B20_LineRead();
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
  uint8_t crc = 0;

  for (uint8_t i = 0; i < len; i++)
  {
    uint8_t inbyte = data[i];

    for (uint8_t j = 0; j < 8U; j++)
    {
      uint8_t mix = (crc ^ inbyte) & 0x01U;
      crc >>= 1;
      if (mix != 0U)
      {
        crc ^= 0x8CU;
      }
      inbyte >>= 1;
    }
  }

  return crc;
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

      rom_mask <<= 1;
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

static void DS18B20_ReadAllTemperatures(void)
{
  Debug_Log("[DS18B20] search start\r\n");
  ds18b20_count = DS18B20_Search(ds18b20_roms, DS18B20_MAX_SENSORS);

  for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++)
  {
    ds18b20_temp_centi[i] = 0;
    ds18b20_temp_ok[i] = false;
  }

  if (ds18b20_count == 0U)
  {
    Debug_Log("[DS18B20] no sensors found\r\n");
    return;
  }

  if (DS18B20_Reset() == 0U)
  {
    Debug_Log("[DS18B20] convert skipped: bus reset failed\r\n");
    return;
  }

  Debug_Log("[DS18B20] sensors found: %u, start conversion\r\n", ds18b20_count);
  DS18B20_WriteByte(0xCCU);
  DS18B20_WriteByte(0x44U);
  HAL_Delay(DS18B20_CONVERT_MS);

  for (uint8_t i = 0; i < ds18b20_count; i++)
  {
    ds18b20_temp_ok[i] = DS18B20_ReadTemperatureCenti(ds18b20_roms[i], &ds18b20_temp_centi[i]);
    if (ds18b20_temp_ok[i] == true)
    {
      Debug_Log("[DS18B20] sensor %u temp_x100=%ld\r\n",
                (unsigned int)(i + 1U), ds18b20_temp_centi[i]);
    }
    else
    {
      Debug_Log("[DS18B20] sensor %u temperature read failed\r\n", (unsigned int)(i + 1U));
    }
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
  Debug_Log("\r\n[BOOT] vetr_srt1.0 start\r\n");
  Debug_Log("[BOOT] SystemClock configured before GPIO/UART init: SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu\r\n",
            HAL_RCC_GetSysClockFreq(), HAL_RCC_GetHCLKFreq(),
            HAL_RCC_GetPCLK1Freq(), HAL_RCC_GetPCLK2Freq());
  Debug_Log("[BOOT] GPIO, DMA, ADC1, ADC2, USART1, USART2, TIM2 init done\r\n");
  DWT_Delay_Init();
  Debug_Log("[BOOT] DWT delay init done\r\n");
  App_StartPowerSequence();
  DS18B20_LineRelease();
  App_StartMeasurements();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (rms_ready != 0U)
    {
      App_ProcessMeasurementWindow();
    }
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
  htim2.Init.Period = 99;
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
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10|GPIO_PIN_14|PWR_OFF_Pin, GPIO_PIN_RESET);

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

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM2)
  {
    return;
  }

  if (rms_ready != 0U)
  {
    return;
  }

  for (uint8_t i = 0; i < ADC_CHANNELS; i++)
  {
    float v = (float)adc_buffer[i];
    sum[i] += v;
    sum_sq[i] += v * v;
  }

  rms_count++;

  if (rms_count >= RMS_SAMPLES)
  {
    frequency_pulses_ready = frequency_pulse_count;
    frequency_pulse_count = 0;
    rms_count = 0;
    rms_ready = 1;
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == Freqency_Pin)
  {
    frequency_pulse_count++;
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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
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
