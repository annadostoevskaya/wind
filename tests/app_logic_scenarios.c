#include <stdio.h>
#include <stdlib.h>

#include "app_logic.h"

#define TEST_ADC_MAX        4095U
#define TEST_VREF           3.0f
#define TEST_BAT_R_TOP      10000.0f
#define TEST_BAT_R_BOTTOM   13000.0f
#define TEST_BAT_CORRECTION 1.10f
#define TEST_BAT_THRESHOLD  3.6f

typedef struct
{
  unsigned int failed_joins;
  unsigned int battery_checks;
  bool joined;
  bool emergency;
} JoinModel;

typedef struct
{
  bool valid;
  uint16_t raw;
  float voltage;
  bool at_rail;
} BatteryModelResult;

static void require_true(bool condition, const char *scenario)
{
  if (!condition)
  {
    fprintf(stderr, "FAIL: %s\n", scenario);
    exit(EXIT_FAILURE);
  }
  printf("PASS: %s\n", scenario);
}

static float model_battery_voltage(uint16_t raw)
{
  return (((float)raw * TEST_VREF) / (float)TEST_ADC_MAX) *
         ((TEST_BAT_R_TOP + TEST_BAT_R_BOTTOM) / TEST_BAT_R_BOTTOM) *
         TEST_BAT_CORRECTION;
}

static BatteryModelResult model_battery_read(bool discard_hal_ok,
                                             bool retained_hal_ok,
                                             uint16_t retained_raw)
{
  AppLogicBatteryAccumulator accumulator;
  BatteryModelResult result = {false, 0U, 0.0f, false};

  AppLogic_BatteryAccumulatorInit(&accumulator);

  if (!discard_hal_ok)
  {
    return result;
  }

  for (uint8_t i = 0U; i < 8U; i++)
  {
    if (!AppLogic_BatteryAccumulatorAdd(&accumulator, retained_hal_ok,
                                        retained_raw, TEST_ADC_MAX))
    {
      return result;
    }
  }

  if (!AppLogic_BatteryAccumulatorAverage(&accumulator, 8U, &result.raw))
  {
    return result;
  }
  result.voltage = model_battery_voltage(result.raw);
  result.at_rail = accumulator.at_rail;
  result.valid = true;
  return result;
}

static BatteryModelResult model_battery_retries(const bool attempt_ok[3],
                                                uint16_t raw,
                                                unsigned int *attempts_used)
{
  BatteryModelResult result = {false, 0U, 0.0f, false};

  *attempts_used = 0U;
  for (uint8_t attempt = 0U; attempt < 3U; attempt++)
  {
    (*attempts_used)++;
    result = model_battery_read(true, attempt_ok[attempt], raw);
    if (result.valid)
    {
      return result;
    }
  }
  return result;
}

static void model_join_event(JoinModel *model, uint32_t event,
                             bool battery_valid, float battery_voltage,
                             bool rotation_window_complete,
                             uint32_t rotation_pulses)
{
  if (AppLogic_JoinConfirmed(event))
  {
    model->joined = true;
    return;
  }
  if ((event & RAK_EVT_JOIN_FAILED) != 0U)
  {
    model->failed_joins++;
    if ((model->failed_joins % 3U) == 0U)
    {
      model->battery_checks++;
      model->emergency =
        AppLogic_ShouldShutdown(battery_valid, battery_voltage,
                                TEST_BAT_THRESHOLD,
                                rotation_window_complete, rotation_pulses);
    }
  }
}

static uint32_t parse_complete_prefix_before_fault(const char *bytes,
                                                   size_t length)
{
  char line[64] = {0};
  size_t line_length = 0U;
  uint32_t events = 0U;

  for (size_t i = 0U; i < length; i++)
  {
    if ((bytes[i] == '\r') || (bytes[i] == '\n'))
    {
      if (line_length > 0U)
      {
        line[line_length] = '\0';
        events |= AppLogic_RakClassifyLine(line);
        line_length = 0U;
      }
    }
    else if (line_length < (sizeof(line) - 1U))
    {
      line[line_length++] = bytes[i];
    }
  }
  return events;
}

int main(void)
{
  JoinModel model = {0U, 0U, false, false};
  BatteryModelResult battery;
  const uint8_t valid_scratchpad[9] = {0x91U, 0x01U, 0x4BU, 0x46U, 0x7FU,
                                       0xFFU, 0x0CU, 0x10U, 0x70U};
  uint8_t bad_scratchpad[9];

  model_join_event(&model, AppLogic_RakClassifyLine("+EVT:JOINED"),
                   true, 4.0f, false, 0U);
  require_true(model.joined, "successful JOIN on first attempt");

  model = (JoinModel){0U, 0U, false, false};
  model_join_event(&model, AppLogic_RakClassifyLine("OK"), true, 4.0f,
                   false, 0U);
  require_true(!model.joined && (model.failed_joins == 0U),
               "bare OK accepts the command but never completes JOIN");

  model_join_event(&model, AppLogic_RakClassifyLine("AT+NJS:1"),
                   true, 4.0f, false, 0U);
  require_true(!model.joined,
               "AT+NJS:1 never substitutes for the exact +EVT:JOINED event");

  model = (JoinModel){0U, 0U, false, false};
  model_join_event(&model, AppLogic_RakClassifyLine("AT_BUSY_ERROR"),
                   true, 4.0f, false, 0U);
  require_true((model.failed_joins == 0U) &&
               (model.battery_checks == 0U) && !model.joined,
               "AT_BUSY_ERROR is not a completed radio JOIN failure");

  model = (JoinModel){0U, 0U, false, false};
  for (unsigned int i = 0U; i < 3U; i++)
  {
    model_join_event(&model,
      AppLogic_RakClassifyLine("+EVT:JOIN_FAILED_RX_TIMEOUT"), true, 4.0f,
      true, 0U);
  }
  require_true((model.failed_joins == 3U) &&
               (model.battery_checks == 1U) && !model.emergency,
               "three failed JOINs check health and restart when battery is healthy");

  model = (JoinModel){0U, 0U, false, false};
  for (unsigned int i = 0U; i < 3U; i++)
  {
    model_join_event(&model, RAK_EVT_JOIN_FAILED, true, 3.59f, true, 1U);
  }
  require_true(!model.emergency,
               "three failed JOINs restart when rotation is present despite low battery");

  model = (JoinModel){0U, 0U, false, false};
  for (unsigned int i = 0U; i < 3U; i++)
  {
    model_join_event(&model, RAK_EVT_JOIN_FAILED, true, 3.59f, true, 0U);
  }
  require_true(model.emergency,
               "three failed JOINs shut down only with low battery and zero rotation");

  require_true((AppLogic_RakClassifyLine("OK") == RAK_EVT_OK) &&
               (AppLogic_RakClassifyLine("OKAY") == 0U) &&
               (AppLogic_RakClassifyLine("NOT_OK") == 0U) &&
               (AppLogic_RakClassifyLine(" OK") == 0U),
               "OK is accepted only as an exact complete line");

  require_true((AppLogic_RakClassifyLine("+EVT:JOINED") == RAK_EVT_JOINED) &&
               (AppLogic_RakClassifyLine("+EVT:JOINED_EXTRA") == 0U),
               "JOINED has no substring false positive");

  require_true((AppLogic_RakClassifyLine("AT+NJS:1") == RAK_EVT_NJS_1) &&
               (AppLogic_RakClassifyLine("AT+NJS:0") == RAK_EVT_NJS_0) &&
               (AppLogic_RakClassifyLine("AT+NJS=1") == RAK_EVT_NJS_1) &&
               (AppLogic_RakClassifyLine("AT+NJS=0") == RAK_EVT_NJS_0),
               "exact NJS status remains available for liveness checks");
  require_true((AppLogic_RakClassifyLine("1") == 0U) &&
               (AppLogic_RakClassifyLineWithNjsContext("1", true) ==
                RAK_EVT_NJS_1) &&
               (AppLogic_RakClassifyLineWithNjsContext("0", true) ==
                RAK_EVT_NJS_0),
               "deprecated bare NJS status is accepted only in reply context");

  require_true(AppLogic_RakClassifyLine("+EVT:JOIN FAILED") ==
               RAK_EVT_JOIN_FAILED,
               "deprecated spaced JOIN failure");
  require_true(AppLogic_RakClassifyLine("+EVT:JOIN_FAILED_RX_TIMEOUT") ==
               RAK_EVT_JOIN_FAILED,
               "current JOIN_FAILED_RX_TIMEOUT response");
  require_true(AppLogic_RakClassifyLine("+EVT:JOIN_FAILED_TX_TIMEOUT") ==
               RAK_EVT_JOIN_FAILED,
               "current JOIN_FAILED_TX_TIMEOUT response");

  require_true((AppLogic_RakClassifyLine("AT_BUSY_ERROR") == RAK_EVT_BUSY) &&
               (AppLogic_RakClassifyLine("AT_DUTYCYLE_RESTRICTED") ==
                RAK_EVT_BUSY) &&
               (AppLogic_RakClassifyLine("AT_DUTYCYCLE_RESTRICTED") ==
                RAK_EVT_BUSY),
               "BUSY and duty-cycle restriction use non-failure backoff");
  require_true(AppLogic_RakClassifyLine("AT_PARAM_ERROR") ==
               RAK_EVT_PARAM_ERROR,
               "parameter rejection is distinguishable from radio failure");
  require_true((AppLogic_RakClassifyLine("AT_ERROR") == RAK_EVT_ERROR) &&
               (AppLogic_RakClassifyLine("AT_RX_ERROR") == RAK_EVT_ERROR) &&
               (AppLogic_RakClassifyLine("AT_ERROR_EXTRA") == 0U),
               "AT errors are exact complete-line events");
  require_true((AppLogic_RakClassifyLine("AT_NO_NETWORK_JOINED") ==
                RAK_EVT_NO_NETWORK) &&
               (AppLogic_RakClassifyLine("AT_NO_NETWORK_JOINED_EXTRA") ==
                0U),
               "network loss is exact and has no substring false positive");

  require_true(AppLogic_RakClassifyLine("+EVT:TX_DONE") == RAK_EVT_SEND_OK,
               "current RUI3 unconfirmed TX_DONE success");
  require_true(AppLogic_RakClassifyLine("+EVT:SEND_CONFIRMED_OK") ==
               RAK_EVT_SEND_OK,
               "current confirmed SEND success");
  require_true(AppLogic_RakClassifyLine("+EVT:SEND_UNCONFIRMED_OK") ==
               RAK_EVT_SEND_OK,
               "current unconfirmed SEND success variant");
  require_true((AppLogic_RakClassifyLine("+EVT:SEND UNCONFIRMED OK") ==
                RAK_EVT_SEND_OK) &&
               (AppLogic_RakClassifyLine("+EVT: SEND CONFIRMED OK") ==
                RAK_EVT_SEND_OK),
               "deprecated spaced SEND success variants");
  require_true(AppLogic_RakClassifyLine(
                 "+EVT:SEND_UNCONFIRMED_FAILED(4)") == RAK_EVT_SEND_FAILED,
               "current final SEND failure with reason suffix");
  require_true(AppLogic_RakClassifyLine("+EVT: SEND CONFIRMED FAILED") ==
               RAK_EVT_SEND_FAILED,
               "deprecated colon-space SEND failure");
  {
    static const char *const send_failures[] = {
      "+EVT:SEND_CONFIRMED_FAILED",
      "+EVT:SEND_UNCONFIRMED_FAILED:4",
      "+EVT:SEND_CONFIRMED_ERROR,4",
      "+EVT:SEND_UNCONFIRMED_ERROR 4",
      "+EVT:SEND_FAILED",
      "+EVT:SEND_ERROR",
      "+EVT:TX_FAILED",
      "+EVT:TX_TIMEOUT"
    };
    bool all_send_failures_classified = true;

    for (size_t i = 0U;
         i < (sizeof(send_failures) / sizeof(send_failures[0])); i++)
    {
      if (AppLogic_RakClassifyLine(send_failures[i]) != RAK_EVT_SEND_FAILED)
      {
        all_send_failures_classified = false;
      }
    }
    require_true(all_send_failures_classified,
                 "all supported final SEND failure families are classified");
  }
  require_true((AppLogic_RakClassifyLine("+EVT:TX_DONE_EXTRA") == 0U) &&
               (AppLogic_RakClassifyLine(
                  "+EVT:SEND_UNCONFIRMED_FAILEDISH") == 0U) &&
               (AppLogic_RakClassifyLine(
                  "PREFIX_AT_NO_NETWORK_JOINED") == 0U) &&
               (AppLogic_RakClassifyLine("AT+NJS:10") == 0U),
               "similar SEND/network lines are not false events");

  {
    uint32_t events = AppLogic_RakClassifyLine("OK") |
                      AppLogic_RakClassifyLine("+EVT:JOINED");
    require_true((events & RAK_EVT_JOINED) != 0U,
                 "OK and JOINED in one parser pass preserve JOIN success");
  }
  {
    uint32_t events = AppLogic_RakClassifyLine("OK") |
                      AppLogic_RakClassifyLine("+EVT:TX_DONE");
    require_true((events & RAK_EVT_SEND_OK) != 0U,
                 "OK and TX_DONE in one parser pass preserve SEND success");
  }

  battery = model_battery_read(true, true, 0U);
  require_true(battery.valid && battery.at_rail &&
               AppLogic_BatteryLow(true, battery.voltage,
                                   TEST_BAT_THRESHOLD),
               "HAL_OK battery raw 0 is valid and classified as low");

  battery = model_battery_read(true, true, 1U);
  require_true(battery.valid && !battery.at_rail &&
               AppLogic_BatteryLow(true, battery.voltage,
                                   TEST_BAT_THRESHOLD),
               "battery raw 1 is valid and low");

  battery = model_battery_read(true, true, 2048U);
  require_true(battery.valid && !battery.at_rail,
               "normal battery ADC result is valid");

  {
    uint16_t threshold_raw = 0U;
    while ((threshold_raw < TEST_ADC_MAX) &&
           (model_battery_voltage(threshold_raw) < TEST_BAT_THRESHOLD))
    {
      threshold_raw++;
    }
    require_true((threshold_raw > 0U) &&
                 AppLogic_BatteryLow(true,
                   model_battery_voltage((uint16_t)(threshold_raw - 1U)),
                   TEST_BAT_THRESHOLD) &&
                 !AppLogic_BatteryLow(true,
                   model_battery_voltage(threshold_raw), TEST_BAT_THRESHOLD),
                 "battery conversion preserves the 3.6 V threshold boundary");
  }

  battery = model_battery_read(true, true, TEST_ADC_MAX);
  require_true(battery.valid && battery.at_rail &&
               !AppLogic_BatteryLow(true, battery.voltage,
                                    TEST_BAT_THRESHOLD),
               "HAL_OK battery raw 4095 is valid saturated high");

  battery = model_battery_read(true, false, 0U);
  require_true(!battery.valid &&
               !AppLogic_BatteryLow(battery.valid, battery.voltage,
                                    TEST_BAT_THRESHOLD),
               "HAL conversion timeout is invalid and never false zero volts");
  battery = model_battery_read(false, true, 2048U);
  require_true(!battery.valid,
               "failed discarded conversion invalidates the ADC attempt");
  {
    const bool all_failed[3] = {false, false, false};
    unsigned int attempts_used = 0U;
    battery = model_battery_retries(all_failed, 2048U, &attempts_used);
    require_true(!battery.valid && (attempts_used == 3U),
                 "battery HAL failure exhausts exactly three attempts");
  }
  {
    const bool succeeds_second[3] = {false, true, false};
    unsigned int attempts_used = 0U;
    battery = model_battery_retries(succeeds_second, 2048U,
                                    &attempts_used);
    require_true(battery.valid && (attempts_used == 2U),
                 "battery retry recovers after an earlier HAL failure");
  }

  require_true(AppLogic_AdcSelectBlock(0U, 0U, 0x00U) ==
               APP_LOGIC_ADC_BLOCK_NONE,
               "DMA pending zero is normal idle, not an error");
  require_true(AppLogic_AdcSelectBlock(0U, 0U, 0x01U) ==
               APP_LOGIC_ADC_BLOCK_HALF,
               "DMA half-only block is processed");
  require_true(AppLogic_AdcSelectBlock(0U, 0U, 0x02U) ==
               APP_LOGIC_ADC_BLOCK_FULL,
               "DMA full-only block is processed");
  require_true(AppLogic_AdcSelectBlock(0U, 0U, 0x03U) ==
               APP_LOGIC_ADC_BLOCK_DISCARD,
               "simultaneous half/full pending discards the damaged window");
  require_true((AppLogic_AdcSelectBlock(0U, 1U, 0x01U) ==
                APP_LOGIC_ADC_BLOCK_DISCARD) &&
               (AppLogic_AdcSelectBlock(0U, 1U, 0x02U) ==
                APP_LOGIC_ADC_BLOCK_DISCARD),
               "repeated half or full callback is treated as overrun");
  require_true(AppLogic_AdcSelectBlock(1U, 0U, 0x00U) ==
               APP_LOGIC_ADC_BLOCK_DISCARD,
               "ADC hardware overrun/error discards and restarts");
  require_true(AppLogic_AdcGenerationStable(17U, 17U) &&
               !AppLogic_AdcGenerationStable(17U, 18U),
               "DMA generation change during processing is detected");
  require_true(!AppLogic_AdcBlockUsable(0U, 0U, 0x00U) &&
               AppLogic_AdcBlockUsable(0U, 0U, 0x01U) &&
               AppLogic_AdcBlockUsable(0U, 0U, 0x02U),
               "ADC block usability distinguishes idle, half and full");

  {
    uint32_t initial_mask = 0x0000FC00UL;
    uint32_t disabled =
      AppLogic_ExtiMaskUpdate(initial_mask, 0x00001000UL, false);
    uint32_t enabled =
      AppLogic_ExtiMaskUpdate(disabled, 0x00001000UL, true);
    require_true(((disabled & 0x00001000UL) == 0U) &&
                 ((disabled & ~0x00001000UL) ==
                  (initial_mask & ~0x00001000UL)) &&
                 (enabled == initial_mask),
                 "EXTI12 masking preserves every other shared EXTI line");
  }
  {
    uint32_t reload = 0U;
    require_true(!AppLogic_RtcWakeReload(0U, &reload) &&
                 AppLogic_RtcWakeReload(1U, &reload) && (reload == 0U) &&
                 AppLogic_RtcWakeReload(10U, &reload) && (reload == 9U) &&
                 AppLogic_RtcWakeReload(65536U, &reload) &&
                 (reload == 65535U) &&
                 !AppLogic_RtcWakeReload(65537U, &reload),
                 "RTC wake reload validates nominal one-to-65536-second range");
  }

  require_true(!AppLogic_RotationStopped(true, 1U),
               "rotation recovers after STOP observation window");
  require_true(AppLogic_RotationStopped(true, 0U) &&
               !AppLogic_RotationStopped(false, 0U),
               "zero rotation requires a completed independent window");
  require_true(AppLogic_ShouldShutdown(true, 3.59f, TEST_BAT_THRESHOLD,
                                      true, 0U),
               "shutdown requires low battery and confirmed zero rotation");
  require_true(!AppLogic_ShouldShutdown(true, 3.59f, TEST_BAT_THRESHOLD,
                                       true, 1U) &&
               !AppLogic_ShouldShutdown(true, 4.0f, TEST_BAT_THRESHOLD,
                                        true, 0U) &&
               !AppLogic_ShouldShutdown(true, 3.59f, TEST_BAT_THRESHOLD,
                                        false, 0U) &&
               !AppLogic_ShouldShutdown(false, 0.0f, TEST_BAT_THRESHOLD,
                                        true, 0U),
               "either condition missing always cancels shutdown");
  require_true(AppLogic_RevolutionsFromPulses(6U, 6U) == 1.0f,
               "six pulses remain one revolution, never RPM");

  require_true(AppLogic_RakClassifyLine("AT_NO_NETWORK_JOINED") ==
               RAK_EVT_NO_NETWORK,
               "LoRaWAN membership loss is classified for immediate reconnect");
  require_true(AppLogic_DeadlineReached(30000U, 30000U) &&
               !AppLogic_UplinkFailureIsCurrent(false),
               "deadlines and late SEND failure accounting remain stable");
  require_true(AppLogic_RingNext(511U, 511U) == 0U,
               "power-of-two UART ring wraps correctly");

  {
    static const char intact_before_fault[] = "+EVT:JOINED\r\n";
    require_true(parse_complete_prefix_before_fault(intact_before_fault,
                 sizeof(intact_before_fault) - 1U) == RAK_EVT_JOINED,
                 "UART fault boundary preserves complete preceding lines");
  }

  for (unsigned int i = 0U; i < sizeof(bad_scratchpad); i++)
  {
    bad_scratchpad[i] = valid_scratchpad[i];
  }
  bad_scratchpad[8] ^= 0x01U;
  require_true((AppLogic_DallasCrc8(valid_scratchpad, 8U) ==
                valid_scratchpad[8]) &&
               (AppLogic_DallasCrc8(bad_scratchpad, 8U) !=
                bad_scratchpad[8]),
               "DS18B20 CRC success and failure");

  return EXIT_SUCCESS;
}
