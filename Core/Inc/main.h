/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ON_AX_Pin GPIO_PIN_2
#define ON_AX_GPIO_Port GPIOE
#define Z_Pin GPIO_PIN_0
#define Z_GPIO_Port GPIOA
#define Y_Pin GPIO_PIN_1
#define Y_GPIO_Port GPIOA
#define X_Pin GPIO_PIN_2
#define X_GPIO_Port GPIOA
#define FIVEV_AN_Pin GPIO_PIN_3
#define FIVEV_AN_GPIO_Port GPIOA
#define V_A_Pin GPIO_PIN_4
#define V_A_GPIO_Port GPIOA
#define I_A_Pin GPIO_PIN_5
#define I_A_GPIO_Port GPIOA
#define V_B_Pin GPIO_PIN_6
#define V_B_GPIO_Port GPIOA
#define I_B_Pin GPIO_PIN_7
#define I_B_GPIO_Port GPIOA
#define V_C_Pin GPIO_PIN_4
#define V_C_GPIO_Port GPIOC
#define I_C_Pin GPIO_PIN_5
#define I_C_GPIO_Port GPIOC
#define Freqency_Pin GPIO_PIN_12
#define Freqency_GPIO_Port GPIOB
#define ON_FR_Pin GPIO_PIN_13
#define ON_FR_GPIO_Port GPIOB
#define ON_RAK_Pin GPIO_PIN_6
#define ON_RAK_GPIO_Port GPIOC
#define ON_AN_Pin GPIO_PIN_10
#define ON_AN_GPIO_Port GPIOC
#define DS_Pin GPIO_PIN_11
#define DS_GPIO_Port GPIOC
#define ON_PWR_DET_Pin GPIO_PIN_12
#define ON_PWR_DET_GPIO_Port GPIOC
#define PWR_Pin GPIO_PIN_0
#define PWR_GPIO_Port GPIOD
#define PWR_OFF_Pin GPIO_PIN_3
#define PWR_OFF_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
