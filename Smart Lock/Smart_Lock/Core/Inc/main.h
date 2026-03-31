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
#include "stm32f1xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define sw_open_Pin GPIO_PIN_1
#define sw_open_GPIO_Port GPIOA
#define sw_close_Pin GPIO_PIN_2
#define sw_close_GPIO_Port GPIOA
#define buzzer_Pin GPIO_PIN_3
#define buzzer_GPIO_Port GPIOA
#define keypad_Pin GPIO_PIN_4
#define keypad_GPIO_Port GPIOA
#define keypadA5_Pin GPIO_PIN_5
#define keypadA5_GPIO_Port GPIOA
#define keypadA6_Pin GPIO_PIN_6
#define keypadA6_GPIO_Port GPIOA
#define keypadA7_Pin GPIO_PIN_7
#define keypadA7_GPIO_Port GPIOA
#define keypadB0_Pin GPIO_PIN_0
#define keypadB0_GPIO_Port GPIOB
#define keypadB1_Pin GPIO_PIN_1
#define keypadB1_GPIO_Port GPIOB
#define keypadB10_Pin GPIO_PIN_10
#define keypadB10_GPIO_Port GPIOB
#define ir_sensor_Pin GPIO_PIN_11
#define ir_sensor_GPIO_Port GPIOB
#define sw_closeB12_Pin GPIO_PIN_12
#define sw_closeB12_GPIO_Port GPIOB
#define sw_openB13_Pin GPIO_PIN_13
#define sw_openB13_GPIO_Port GPIOB
#define IR_Up_Pin GPIO_PIN_15
#define IR_Up_GPIO_Port GPIOB
#define in2_Pin GPIO_PIN_9
#define in2_GPIO_Port GPIOA
#define in1_Pin GPIO_PIN_10
#define in1_GPIO_Port GPIOA
#define IR_down_Pin GPIO_PIN_15
#define IR_down_GPIO_Port GPIOA
#define in4_Pin GPIO_PIN_4
#define in4_GPIO_Port GPIOB
#define in3_Pin GPIO_PIN_5
#define in3_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define sw_closeB12_Pin GPIO_PIN_12
#define sw_closeB12_GPIO_Port GPIOB
#define sw_openB13_Pin GPIO_PIN_13
#define sw_openB13_GPIO_Port GPIOB
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
