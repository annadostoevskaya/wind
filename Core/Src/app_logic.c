#include "app_logic.h"

#include <stddef.h>
#include <string.h>

bool AppLogic_LineStartsWith(const char *line, const char *prefix)
{
  if ((line == NULL) || (prefix == NULL))
  {
    return false;
  }

  while (*prefix != '\0')
  {
    if (*line != *prefix)
    {
      return false;
    }
    line++;
    prefix++;
  }
  return true;
}

static bool AppLogic_EventPrefixWithSuffix(const char *line,
                                           const char *prefix)
{
  size_t length;
  char suffix;

  if (!AppLogic_LineStartsWith(line, prefix))
  {
    return false;
  }

  length = strlen(prefix);
  suffix = line[length];
  return (suffix == '\0') || (suffix == '(') || (suffix == ':') ||
         (suffix == ',') || (suffix == ' ');
}

uint32_t AppLogic_RakClassifyLine(const char *line)
{
  if (line == NULL)
  {
    return 0U;
  }

  if (strcmp(line, "OK") == 0)
  {
    return RAK_EVT_OK;
  }
  if (strcmp(line, "+EVT:JOINED") == 0)
  {
    return RAK_EVT_JOINED;
  }
  if ((strcmp(line, "+EVT:JOIN FAILED") == 0) ||
      AppLogic_LineStartsWith(line, "+EVT:JOIN_FAILED"))
  {
    return RAK_EVT_JOIN_FAILED;
  }
  if ((strcmp(line, "AT+NJS:1") == 0) ||
      (strcmp(line, "AT+NJS=1") == 0) ||
      (strcmp(line, "+NJS:1") == 0))
  {
    return RAK_EVT_NJS_1;
  }
  if ((strcmp(line, "AT+NJS:0") == 0) ||
      (strcmp(line, "AT+NJS=0") == 0) ||
      (strcmp(line, "+NJS:0") == 0))
  {
    return RAK_EVT_NJS_0;
  }

  /* Current RUI3 uses TX_DONE for an unconfirmed uplink.  The remaining
     exact strings cover current confirmed and deprecated RAK3172 firmware. */
  if ((strcmp(line, "+EVT:TX_DONE") == 0) ||
      (strcmp(line, "+EVT:SEND_CONFIRMED_OK") == 0) ||
      (strcmp(line, "+EVT:SEND_UNCONFIRMED_OK") == 0) ||
      (strcmp(line, "+EVT:SEND CONFIRMED OK") == 0) ||
      (strcmp(line, "+EVT:SEND UNCONFIRMED OK") == 0) ||
      (strcmp(line, "+EVT: SEND CONFIRMED OK") == 0) ||
      (strcmp(line, "+EVT: SEND UNCONFIRMED OK") == 0))
  {
    return RAK_EVT_SEND_OK;
  }

  if (AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_CONFIRMED_FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_UNCONFIRMED_FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_CONFIRMED_ERROR") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_UNCONFIRMED_ERROR") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND CONFIRMED FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND UNCONFIRMED FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT: SEND CONFIRMED FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT: SEND UNCONFIRMED FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:SEND_ERROR") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:TX_FAILED") ||
      AppLogic_EventPrefixWithSuffix(line, "+EVT:TX_TIMEOUT"))
  {
    return RAK_EVT_SEND_FAILED;
  }

  if (strcmp(line, "AT_NO_NETWORK_JOINED") == 0)
  {
    return RAK_EVT_NO_NETWORK;
  }
  if ((strcmp(line, "AT_BUSY_ERROR") == 0) ||
      (strcmp(line, "AT_DUTYCYLE_RESTRICTED") == 0) ||
      (strcmp(line, "AT_DUTYCYCLE_RESTRICTED") == 0))
  {
    return RAK_EVT_BUSY;
  }
  if ((strcmp(line, "AT_PARAM_ERROR") == 0) ||
      (strcmp(line, "AT_TEST_PARAM_OVERFLOW") == 0))
  {
    return RAK_EVT_PARAM_ERROR;
  }
  if ((strcmp(line, "AT_ERROR") == 0) ||
      (strcmp(line, "AT_RX_ERROR") == 0))
  {
    return RAK_EVT_ERROR;
  }
  return 0U;
}

uint32_t AppLogic_RakClassifyLineWithNjsContext(const char *line,
                                                bool njs_reply_expected)
{
  uint32_t event = AppLogic_RakClassifyLine(line);

  /* Deprecated pre-RUI3 firmware returns a bare 0/1 to AT+NJS=?.  Those
     tokens are ambiguous globally and are accepted only while that reply is
     explicitly expected by the JOIN state machine. */
  if ((event == 0U) && njs_reply_expected && (line != NULL))
  {
    if (strcmp(line, "1") == 0)
    {
      return RAK_EVT_NJS_1;
    }
    if (strcmp(line, "0") == 0)
    {
      return RAK_EVT_NJS_0;
    }
  }
  return event;
}

bool AppLogic_JoinConfirmed(uint32_t events)
{
  return (events & RAK_EVT_JOINED) != 0U;
}

bool AppLogic_BatteryLow(bool valid, float voltage, float threshold)
{
  return valid && (voltage < threshold);
}

bool AppLogic_ShouldShutdown(bool battery_valid, float voltage,
                             float threshold, bool rotation_window_complete,
                             uint32_t rotation_pulses)
{
  return AppLogic_BatteryLow(battery_valid, voltage, threshold) &&
         AppLogic_RotationStopped(rotation_window_complete, rotation_pulses);
}

bool AppLogic_AdcRawAtRail(uint16_t raw, uint16_t maximum)
{
  return (raw == 0U) || (raw >= maximum);
}

void AppLogic_BatteryAccumulatorInit(AppLogicBatteryAccumulator *accumulator)
{
  if (accumulator != NULL)
  {
    accumulator->sum = 0U;
    accumulator->count = 0U;
    accumulator->at_rail = false;
  }
}

bool AppLogic_BatteryAccumulatorAdd(AppLogicBatteryAccumulator *accumulator,
                                    bool conversion_ok, uint16_t raw,
                                    uint16_t maximum)
{
  if ((accumulator == NULL) || !conversion_ok)
  {
    return false;
  }

  accumulator->sum += raw;
  accumulator->count++;
  if (AppLogic_AdcRawAtRail(raw, maximum))
  {
    accumulator->at_rail = true;
  }
  return true;
}

bool AppLogic_BatteryAccumulatorAverage(
  const AppLogicBatteryAccumulator *accumulator, uint8_t expected_count,
  uint16_t *average)
{
  if ((accumulator == NULL) || (average == NULL) || (expected_count == 0U) ||
      (accumulator->count != expected_count))
  {
    return false;
  }

  *average = (uint16_t)((accumulator->sum + (expected_count / 2U)) /
                        expected_count);
  return true;
}

bool AppLogic_DeadlineReached(uint32_t now, uint32_t deadline)
{
  return ((int32_t)(now - deadline) >= 0);
}

bool AppLogic_UplinkFailureIsCurrent(bool transaction_pending)
{
  return transaction_pending;
}

float AppLogic_RevolutionsFromPulses(uint32_t pulses,
                                    uint32_t pulses_per_revolution)
{
  return (float)pulses / (float)pulses_per_revolution;
}

bool AppLogic_RotationStopped(bool window_complete, uint32_t pulses)
{
  return window_complete && (pulses == 0U);
}

uint16_t AppLogic_RingNext(uint16_t head, uint16_t size_mask)
{
  return (uint16_t)((head + 1U) & size_mask);
}

AppLogicAdcBlockAction AppLogic_AdcSelectBlock(uint8_t dma_error,
                                               uint8_t dma_overrun,
                                               uint8_t pending_mask)
{
  if ((dma_error != 0U) || (dma_overrun != 0U))
  {
    return APP_LOGIC_ADC_BLOCK_DISCARD;
  }

  switch (pending_mask)
  {
    case 0x00U: return APP_LOGIC_ADC_BLOCK_NONE;
    case 0x01U: return APP_LOGIC_ADC_BLOCK_HALF;
    case 0x02U: return APP_LOGIC_ADC_BLOCK_FULL;
    case 0x03U:
    default: return APP_LOGIC_ADC_BLOCK_DISCARD;
  }
}

bool AppLogic_AdcBlockUsable(uint8_t dma_error, uint8_t dma_overrun,
                             uint8_t pending_mask)
{
  AppLogicAdcBlockAction action =
    AppLogic_AdcSelectBlock(dma_error, dma_overrun, pending_mask);
  return (action == APP_LOGIC_ADC_BLOCK_HALF) ||
         (action == APP_LOGIC_ADC_BLOCK_FULL);
}

bool AppLogic_AdcGenerationStable(uint32_t before, uint32_t after)
{
  return before == after;
}

uint32_t AppLogic_ExtiMaskUpdate(uint32_t current_mask,
                                 uint32_t line_mask, bool enable)
{
  if (enable)
  {
    return current_mask | line_mask;
  }
  return current_mask & ~line_mask;
}

bool AppLogic_RtcWakeReload(uint32_t seconds, uint32_t *reload)
{
  if ((reload == NULL) || (seconds == 0U) || (seconds > 65536U))
  {
    return false;
  }
  *reload = seconds - 1U;
  return true;
}

uint8_t AppLogic_DallasCrc8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0U;

  if (data == NULL)
  {
    return 0U;
  }

  for (uint8_t i = 0U; i < length; i++)
  {
    uint8_t inbyte = data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01U);
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
