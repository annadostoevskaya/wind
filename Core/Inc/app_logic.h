#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAK_EVT_OK              (1UL << 0)
#define RAK_EVT_JOINED          (1UL << 1)
#define RAK_EVT_JOIN_FAILED     (1UL << 2)
#define RAK_EVT_BUSY            (1UL << 3)
#define RAK_EVT_ERROR           (1UL << 4)
#define RAK_EVT_NJS_1           (1UL << 5)
#define RAK_EVT_NJS_0           (1UL << 6)
#define RAK_EVT_SEND_OK         (1UL << 7)
#define RAK_EVT_SEND_FAILED     (1UL << 8)
#define RAK_EVT_NO_NETWORK      (1UL << 9)
#define RAK_EVT_PARAM_ERROR     (1UL << 10)

typedef enum
{
  APP_LOGIC_ADC_BLOCK_NONE = 0,
  APP_LOGIC_ADC_BLOCK_HALF,
  APP_LOGIC_ADC_BLOCK_FULL,
  APP_LOGIC_ADC_BLOCK_DISCARD
} AppLogicAdcBlockAction;

typedef struct
{
  uint32_t sum;
  uint8_t count;
  bool at_rail;
} AppLogicBatteryAccumulator;

bool AppLogic_LineStartsWith(const char *line, const char *prefix);
uint32_t AppLogic_RakClassifyLine(const char *line);
uint32_t AppLogic_RakClassifyLineWithNjsContext(const char *line,
                                                bool njs_reply_expected);

bool AppLogic_BatteryLow(bool valid, float voltage, float threshold);
bool AppLogic_AdcRawAtRail(uint16_t raw, uint16_t maximum);
void AppLogic_BatteryAccumulatorInit(AppLogicBatteryAccumulator *accumulator);
bool AppLogic_BatteryAccumulatorAdd(AppLogicBatteryAccumulator *accumulator,
                                    bool conversion_ok, uint16_t raw,
                                    uint16_t maximum);
bool AppLogic_BatteryAccumulatorAverage(
  const AppLogicBatteryAccumulator *accumulator, uint8_t expected_count,
  uint16_t *average);
bool AppLogic_DeadlineReached(uint32_t now, uint32_t deadline);
bool AppLogic_UplinkFailureIsCurrent(bool transaction_pending);
float AppLogic_RevolutionsFromPulses(uint32_t pulses,
                                    uint32_t pulses_per_revolution);
bool AppLogic_RotationStopped(bool window_complete, uint32_t pulses);
bool AppLogic_ShouldReconnect(bool explicit_network_loss,
                              uint8_t consecutive_failures,
                              uint8_t failure_limit);
uint16_t AppLogic_RingNext(uint16_t head, uint16_t size_mask);

AppLogicAdcBlockAction AppLogic_AdcSelectBlock(uint8_t dma_error,
                                               uint8_t dma_overrun,
                                               uint8_t pending_mask);
bool AppLogic_AdcBlockUsable(uint8_t dma_error, uint8_t dma_overrun,
                             uint8_t pending_mask);
bool AppLogic_AdcGenerationStable(uint32_t before, uint32_t after);

uint32_t AppLogic_ExtiMaskUpdate(uint32_t current_mask,
                                 uint32_t line_mask, bool enable);
bool AppLogic_RtcWakeReload(uint32_t seconds, uint32_t *reload);
uint8_t AppLogic_DallasCrc8(const uint8_t *data, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_H */
