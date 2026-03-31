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
#include <stdint.h>
#include "string.h"
#include "lcd_i2c_quang.h"
#include "keypad_3x4_quang.h"
#include "fonts.h"
#include "ssd1306.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define R1_GPIO_Port  GPIOB 
#define R2_GPIO_Port	GPIOB
#define R3_GPIO_Port	GPIOB
#define R4_GPIO_Port 	GPIOA

#define R1_Pin	GPIO_PIN_10
#define R2_Pin	GPIO_PIN_1
#define R3_Pin	GPIO_PIN_0
#define R4_Pin	GPIO_PIN_7

#define C1_GPIO_Port	GPIOA
#define C2_GPIO_Port	GPIOA
#define C3_GPIO_Port 	GPIOA


#define C1_Pin	GPIO_PIN_6
#define C2_Pin	GPIO_PIN_5
#define C3_Pin	GPIO_PIN_4 

#define RC522_CS_GPIO_Port  GPIOA 
#define RC522_CS_Pin        GPIO_PIN_11 
#define RC522_RST_GPIO_Port GPIOA 
#define RC522_RST_Pin       GPIO_PIN_12 

#define IR_Pin           GPIO_PIN_11  // IR cam bien cua 1 (GPIOB) - phong ngang cua
#define IR_INSIDE1_Pin   GPIO_PIN_11  // IR phia TRONG cua 1 (PB11) - nguoi ra ngoai
#define IR_INSIDE1_Port  GPIOB

// ========= CUA 2 (PHONG MO) =========
// TIM2_CH2  : PB3  (PWM dong co cua 2 - can AFIO remap)
// Motor IN3 : PB4  (chieu mo)
// Motor IN4 : PB5  (chieu dong)
// SW mo   (CTHT mo)  : PA12 (PULLUP, cham -> RESET)
// SW dong (CTHT dong): PA11 (PULLUP, cham -> RESET)
// IR phong ngang cua 2 / IR phia TRONG: PB15 (PULLUP, co vat -> RESET)
// IR tiep can cua 2 (phia ngoai)      : PA15 (PULLUP, co nguoi -> RESET) -> Face ID
#define DOOR2_IN3_Pin        GPIO_PIN_4
#define DOOR2_IN4_Pin        GPIO_PIN_5
#define DOOR2_IN3_Port       GPIOB
#define DOOR2_IN4_Port       GPIOB
// PA11/PA12 la chan USB D-/D+ tren STM32F103 -> co pull-up phancung USB -> KHONG dung lam GPIO doc switch!
// Chuyen sang PB12 (sw mo) va PB13 (sw dong) - chan GPIO thuan, khong co phan cung dac biet
#define DOOR2_SW_OPEN_Pin    GPIO_PIN_12  // PB12: CTHT mo cua 2
#define DOOR2_SW_CLOSE_Pin   GPIO_PIN_13  // PB13: CTHT dong cua 2
#define DOOR2_SW_Port        GPIOB
#define DOOR2_IR_Pin         GPIO_PIN_15  // PB15: IR phong ngang + IR phia trong cua 2
#define DOOR2_IR_Port        GPIOB
#define IR_APPROACH_Pin      GPIO_PIN_15  // IR tiep can cua 2 - phia ngoai (PA15)
#define IR_APPROACH_Port     GPIOA
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
//SPI_HandleTypeDef hspi2; // Dummy hspi2 de MFRC522.c bien dich duoc (Neu muon dung the tu, ban can enable SPI2 trong CubeMX!)
I2C_HandleTypeDef hi2c1; /* Thêm lại I2C handle cho LCD */
I2C_LCD_HandleTypeDef lcd1;

// ========== CUA 1 ==========
uint8_t door_state = 0;
uint32_t door_timer = 0;
uint8_t door_lcd_updated = 0;

// ========== CUA 2 (PHONG MO) ==========
uint8_t  door2_state = 0;   // 0=dong, 1=dang mo, 2=da mo (dang phau thuat), 3=dang dong
uint32_t door2_timer = 0;
uint8_t  door2_updated = 0;
char     doctor_name[32] = ""; // Ten bac si da xac minh

// ── SURGERY STATE MACHINE ──
uint8_t  is_surgery_ongoing = 0; // Co trang thai ca mo (1=dang mo, 0=trong)

// IR tiep can cua 2 (PA15): theo doi trang thai de gui lenh F mot lan
uint8_t  ir_approach_sent = 0; // Da gui lenh F chua (tranh gui nhieu lan)

uint8_t rxData;
uint8_t stt;
char pass[] = "1234"; 
char input_pass[21];
int input_idx = 0;
int trang_thai = 0;
uint32_t trang_thai_cho;

uint16_t sai_pass = 0;

// Buffer nhan UART de doc ten bac si
char uart_buf[64];
uint8_t uart_buf_idx = 0;

Keypad_Cfg_t myKeypad = {
	.R_Port = {R1_GPIO_Port, R2_GPIO_Port, R3_GPIO_Port, R4_GPIO_Port},
	.R_Pin = {R1_Pin, R2_Pin, R3_Pin, R4_Pin},
	.C_Port = {C1_GPIO_Port, C2_GPIO_Port, C3_GPIO_Port},
  .C_Pin  = {C1_Pin, C2_Pin, C3_Pin}
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
static void MX_USART1_UART_Init(void);
void ESP_SendStatus(uint8_t c) { HAL_UART_Transmit(&huart1, &c, 1, 100); }
/* Bien co: sau khi cua 1 dong xong, chuyen sang layer 2 */
uint8_t pending_layer2 = 0;

void menu(){
    lcd_clear(&lcd1);
    lcd_gotoxy(&lcd1, 0, 0);
    lcd_puts(&lcd1, "=== CUA VAO ====");
    lcd_gotoxy(&lcd1, 0, 1);
    lcd_puts(&lcd1, "Nhap mat khau:");
    lcd_gotoxy(&lcd1, 0, 2);
    lcd_puts(&lcd1, "Bam # de xac nhan");
    lcd_gotoxy(&lcd1, 0, 3);
    lcd_puts(&lcd1, "Bam * de xoa");
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
		HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
		__HAL_TIM_MOE_ENABLE(&htim1);
		HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2); // Cua 2
		
		lcd1.hi2c = &hi2c1;
    lcd1.address = 0x4E;
    lcd_init(&lcd1);

    // OLED cho cua 2 - dung chung hi2c1 voi LCD (khac dia chi I2C)
    ssd1306_Init(&hi2c1);
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("CUA PHONG MO", Font_7x10, White);
    ssd1306_SetCursor(0, 14);
    ssd1306_WriteString("Vui Long Dua", Font_7x10, White);
    ssd1306_SetCursor(0, 26);
    ssd1306_WriteString("Khuon Mat", Font_7x10, White);
    ssd1306_UpdateScreen(&hi2c1);

    menu();
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
		ESP_SendStatus('A');

		/* Cau hinh CTHT cua 1: PA1=mo, PA2=dong (PULLUP) */
		/* Cau hinh cua 2: PA12=sw_mo, PA11=sw_dong, PB15=IR, PB4=IN3, PB5=IN4 */
		{
				GPIO_InitTypeDef sw_cfg = {0};

				// --- Cua 1: PA1 (sw mo), PA2 (sw dong) – input PULLUP ---
				sw_cfg.Pin  = GPIO_PIN_1 | GPIO_PIN_2;
				sw_cfg.Mode = GPIO_MODE_INPUT;
				sw_cfg.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(GPIOA, &sw_cfg);

				// --- Cua 2: PB12 (sw mo), PB13 (sw dong) – input PULLUP ---
				// (Khong dung PA11/PA12 vi la chan USB co pull-up phan cung)
				sw_cfg.Pin  = DOOR2_SW_OPEN_Pin | DOOR2_SW_CLOSE_Pin;
				sw_cfg.Mode = GPIO_MODE_INPUT;
				sw_cfg.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(DOOR2_SW_Port, &sw_cfg);

				// --- Cua 2: PB15 IR cam bien – input PULLUP ---
				sw_cfg.Pin  = DOOR2_IR_Pin;
				sw_cfg.Mode = GPIO_MODE_INPUT;
				sw_cfg.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(DOOR2_IR_Port, &sw_cfg);

				// --- IR tiep can cua 2: PA15 – input PULLUP ---
				sw_cfg.Pin  = IR_APPROACH_Pin;
				sw_cfg.Mode = GPIO_MODE_INPUT;
				sw_cfg.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(IR_APPROACH_Port, &sw_cfg);

				// --- Cua 2: PB4 (IN3), PB5 (IN4) – output PP ---
				sw_cfg.Pin   = DOOR2_IN3_Pin | DOOR2_IN4_Pin;
				sw_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
				sw_cfg.Pull  = GPIO_NOPULL;
				sw_cfg.Speed = GPIO_SPEED_FREQ_LOW;
				HAL_GPIO_Init(GPIOB, &sw_cfg);
				HAL_GPIO_WritePin(GPIOB, DOOR2_IN3_Pin | DOOR2_IN4_Pin, GPIO_PIN_RESET);
		}
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	
	
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
		// =====================================================
		// CUA 1 STATE MACHINE (Limit Switch Based)
		//   State 0: Cua dong
		//          → IR PB11 (phia trong) phat hien nguoi -> tu mo
		//   State 1: Dang mo  -> TIM1 PWM den khi sw_open (PA1)
		//   State 2: Cua da mo -> cho IR 5s roi dong
		//   State 3: Dang dong -> TIM1 PWM den khi sw_close (PA2)
		// =====================================================

		/* --- CUA 1: Tu dong mo khi co nguoi tu TRONG ra (IR PB11) --- */
		if (door_state == 0) {
				if (HAL_GPIO_ReadPin(IR_INSIDE1_Port, IR_INSIDE1_Pin) == GPIO_PIN_RESET) {
						// Co nguoi phia trong -> mo cua 1
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "CUA 1: TU MO");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Nguoi ra ngoai");
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
						ESP_SendStatus('U');
						door_lcd_updated = 0;
						door_state = 1;
						door_timer = HAL_GetTick();
				}
		}

		if (door_state == 1) {
				if (door_lcd_updated != 1) {
						door_lcd_updated = 1;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "CUA 1: DANG MO");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Dong co dang quay");
				}
				__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 999);
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);

				// CTHT cua 1: NO->PA1, C->GND, PULLUP => nhan xuong LOW=RESET -> dung
				if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET || (HAL_GetTick() - door_timer > 15000)) {
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
						door_state = 2;
						door_timer = HAL_GetTick();
						door_lcd_updated = 0;
				}

		} else if (door_state == 2) {
				if (door_lcd_updated != 2) {
						door_lcd_updated = 2;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "CUA 1: DA MO");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Cho nguoi di vao");
				}
				if (HAL_GPIO_ReadPin(GPIOB, IR_Pin) == GPIO_PIN_RESET) {
						door_timer = HAL_GetTick();
				}
				if (HAL_GetTick() - door_timer > 5000) {
						door_state = 3;
						door_timer = HAL_GetTick();
						door_lcd_updated = 0;
				}

		} else if (door_state == 3) {
				if (door_lcd_updated != 3) {
						door_lcd_updated = 3;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "CUA 1: DANG DONG");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Dong co dang quay");
				}
				if (HAL_GPIO_ReadPin(GPIOB, IR_Pin) == GPIO_PIN_RESET) {
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
						door_state = 1;
						door_timer = HAL_GetTick();
						door_lcd_updated = 0;
				} else {
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 999);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

						// CTHT cua 1: NO->PA2, C->GND, PULLUP => nhan xuong LOW=RESET -> dung
				if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_RESET || (HAL_GetTick() - door_timer > 15000)) {
								__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
								HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
								HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
								HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
								ESP_SendStatus('L');
								door_state = 0;
								door_lcd_updated = 0;
								
								/* Cua 1 phia ngoai da dong xong -> tro ve Menu (nhap mat khau) */
								trang_thai = 0;
								input_idx = 0;
								menu();
						}
				}
		}

		// =====================================================
		// CUA 2 STATE MACHINE (Phong Mo - TIM2_CH2 = PB3)
		//   State 0: Dong  – OLED: "Vui Long Dua Khuon Mat"
		//             IR PA15 (tiep can) phat hien nguoi -> gui lenh F (Face ID)
		//   State 1: Dang mo  -> PWM TIM2, cho PA12 (sw mo)
		//   State 2: Da mo    -> OLED: "Dang Phau Thuat / Bac si: XXX"
		//             IR PB15 reset timer khi co nguoi, dong sau 5s
		//   State 3: Dang dong -> PWM TIM2, cho PA11 (sw dong)
		//             IR PB15 phat hien -> mo lai
		// Motor: PB4=IN3 (mo), PB5=IN4 (dong)
		// =====================================================

		/* --- CUA 2: Tu dong mo khi co nguoi tu TRONG ra (IR PB15) --- */
		if (door2_state == 0) {
				if (HAL_GPIO_ReadPin(DOOR2_IR_Port, DOOR2_IR_Pin) == GPIO_PIN_RESET) {
						// Co nguoi phia trong cua 2 -> mo cua 2 ngay (khong can Face ID)
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CUA 2: TU MO", Font_7x10, White);
						ssd1306_SetCursor(0, 14);
						ssd1306_WriteString("Nguoi Ra Ngoai", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
						ir_approach_sent = 0; // Reset co de cho phep Face ID lan sau
						door2_state = 1;
						door2_timer = HAL_GetTick();
						door2_updated = 0;
				} else {
						/* --- IR TIEP CAN (PA15): Kich hoat Face ID khi co nguoi den gan tu phia ngoai --- */
						if (HAL_GPIO_ReadPin(IR_APPROACH_Port, IR_APPROACH_Pin) == GPIO_PIN_RESET) {
								// Co nguoi tiep can phia ngoai: gui lenh F mot lan
								if (ir_approach_sent == 0) {
										ir_approach_sent = 1;
										ESP_SendStatus('F'); /* Yeu cau Face ID cho Cua 2 */
										ssd1306_Fill(Black);
										ssd1306_SetCursor(0, 0);
										ssd1306_WriteString("Nhan Dien", Font_7x10, White);
										ssd1306_SetCursor(0, 14);
										ssd1306_WriteString("Khuon Mat...", Font_7x10, White);
										ssd1306_UpdateScreen(&hi2c1);
								}
						} else {
								// Khong con nguoi o PA15: reset co de lan sau co the gui lai
								if (ir_approach_sent != 0) {
										ir_approach_sent = 0;
										// OLED tro ve man hinh cho
										ssd1306_Fill(Black);
										ssd1306_SetCursor(0, 0);
										ssd1306_WriteString("CUA PHONG MO", Font_7x10, White);
										ssd1306_SetCursor(0, 14);
										ssd1306_WriteString("Vui Long Dua", Font_7x10, White);
										ssd1306_SetCursor(0, 26);
										ssd1306_WriteString("Khuon Mat", Font_7x10, White);
										ssd1306_UpdateScreen(&hi2c1);
								}
						}
				}
		}

		if (door2_state == 1) { // Dang mo cua 2
				if (door2_updated != 1) {
						door2_updated = 1;
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CUA 2: DANG MO", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
				}
				// Motor cua 2 chieu mo: IN3(PB4)=1, IN4(PB5)=0
				__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 999);
				HAL_GPIO_WritePin(DOOR2_IN3_Port, DOOR2_IN3_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(DOOR2_IN4_Port, DOOR2_IN4_Pin, GPIO_PIN_RESET);

				// CTHT cua 2 mo: NO->PA12, C->GND, PULLUP => nhan xuong LOW=RESET -> dung
				if (HAL_GPIO_ReadPin(DOOR2_SW_Port, DOOR2_SW_OPEN_Pin) == GPIO_PIN_RESET
					|| (HAL_GetTick() - door2_timer > 15000)) {
						__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);
						HAL_GPIO_WritePin(DOOR2_IN3_Port, DOOR2_IN3_Pin, GPIO_PIN_RESET);
						HAL_GPIO_WritePin(DOOR2_IN4_Port, DOOR2_IN4_Pin, GPIO_PIN_RESET);
						door2_state = 2;
						door2_timer = HAL_GetTick();
						door2_updated = 0;
				}

		} else if (door2_state == 2) { // Cua 2 da mo - cho nguoi di qua
				if (door2_updated != 2) {
						door2_updated = 2;
						if (is_surgery_ongoing) {
								char oled_line2[32];
								snprintf(oled_line2, sizeof(oled_line2), "BS: %s", doctor_name);
								ssd1306_Fill(Black);
								ssd1306_SetCursor(0, 0);
								ssd1306_WriteString("DANG PHAU THUAT", Font_7x10, White);
								ssd1306_SetCursor(0, 14);
								ssd1306_WriteString(oled_line2, Font_7x10, White);
								ssd1306_SetCursor(0, 28);
								ssd1306_WriteString("Man hinh KHOA", Font_7x10, White);
								ssd1306_UpdateScreen(&hi2c1);
						} else {
								ssd1306_Fill(Black);
								ssd1306_SetCursor(0, 0);
								ssd1306_WriteString("CUA 2: DA MO", Font_7x10, White);
								ssd1306_UpdateScreen(&hi2c1);
						}
				}
				// IR PB15 hoac IR PA15 phat hien nguoi -> reset timer (giu cua mo)
				if (HAL_GPIO_ReadPin(DOOR2_IR_Port, DOOR2_IR_Pin) == GPIO_PIN_RESET
					|| HAL_GPIO_ReadPin(IR_APPROACH_Port, IR_APPROACH_Pin) == GPIO_PIN_RESET) {
						door2_timer = HAL_GetTick();
				}
				// Tu dong dong sau 5s khong co nguoi
				if (HAL_GetTick() - door2_timer > 5000) {
						door2_state = 3;
						door2_timer = HAL_GetTick();
						door2_updated = 0;
				}

		} else if (door2_state == 3) { // Dang dong cua 2
				if (door2_updated != 3) {
						door2_updated = 3;
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CUA 2: DANG DONG", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
				}
				// IR phat hien nguoi khi dang dong -> mo lai
				if (HAL_GPIO_ReadPin(DOOR2_IR_Port, DOOR2_IR_Pin) == GPIO_PIN_RESET
					|| HAL_GPIO_ReadPin(IR_APPROACH_Port, IR_APPROACH_Pin) == GPIO_PIN_RESET) {
						__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);
						HAL_GPIO_WritePin(DOOR2_IN3_Port, DOOR2_IN3_Pin, GPIO_PIN_RESET);
						HAL_GPIO_WritePin(DOOR2_IN4_Port, DOOR2_IN4_Pin, GPIO_PIN_RESET);
						door2_state = 1;
						door2_timer = HAL_GetTick();
						door2_updated = 0;
				} else {
						// Motor cua 2 chieu dong: IN3(PB4)=0, IN4(PB5)=1
						__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 999);
						HAL_GPIO_WritePin(DOOR2_IN3_Port, DOOR2_IN3_Pin, GPIO_PIN_RESET);
						HAL_GPIO_WritePin(DOOR2_IN4_Port, DOOR2_IN4_Pin, GPIO_PIN_SET);

						if (HAL_GPIO_ReadPin(DOOR2_SW_Port, DOOR2_SW_CLOSE_Pin) == GPIO_PIN_RESET
							|| (HAL_GetTick() - door2_timer > 15000)) {
								__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);
								HAL_GPIO_WritePin(DOOR2_IN3_Port, DOOR2_IN3_Pin, GPIO_PIN_RESET);
								HAL_GPIO_WritePin(DOOR2_IN4_Port, DOOR2_IN4_Pin, GPIO_PIN_RESET);

								if (is_surgery_ongoing) {
										// ── CA MO DANG DIEN RA: Cua dong xong -> hien lai "Dang Phau Thuat" ──
										door2_state = 0;
										door2_updated = 0;
										char oled_line2[32];
										snprintf(oled_line2, sizeof(oled_line2), "BS: %s", doctor_name);
										ssd1306_Fill(Black);
										ssd1306_SetCursor(0, 0);
										ssd1306_WriteString("DANG PHAU THUAT", Font_7x10, White);
										ssd1306_SetCursor(0, 14);
										ssd1306_WriteString(oled_line2, Font_7x10, White);
										ssd1306_SetCursor(0, 28);
										ssd1306_WriteString("Man hinh KHOA", Font_7x10, White);
										ssd1306_UpdateScreen(&hi2c1);
								} else {
										// ── KHONG CO CA MO: Reset ve man hinh cho ──
										door2_state = 0;
										door2_updated = 0;
										memset(doctor_name, 0, sizeof(doctor_name));
										ssd1306_Fill(Black);
										ssd1306_SetCursor(0, 0);
										ssd1306_WriteString("CUA PHONG MO", Font_7x10, White);
										ssd1306_SetCursor(0, 14);
										ssd1306_WriteString("Vui Long Dua", Font_7x10, White);
										ssd1306_SetCursor(0, 26);
										ssd1306_WriteString("Khuon Mat", Font_7x10, White);
										ssd1306_UpdateScreen(&hi2c1);
								}
						}
				}
		}

		
		/* Nhan lenh tu ESP32 qua UART1 */
		__HAL_UART_CLEAR_OREFLAG(&huart1);
		if (HAL_UART_Receive(&huart1, &rxData, 1, 5) == HAL_OK) {
				if (rxData == 'O') {
						// Lenh mo CUA 1 tu Web/App
						trang_thai = 0;
						input_idx = 0;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "LENH TU WEB");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, ">> Mo Cua 1...");
						HAL_Delay(800);
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
						ESP_SendStatus('U');
						door_lcd_updated = 0;
						door_state = 1;
						door_timer = HAL_GetTick();
				}
				else if (rxData == 'G') {
						// ── BAT DAU CA MO: Face ID OK -> luu ten BS, bat co surgery ──
						uart_buf_idx = 0;
						memset(uart_buf, 0, sizeof(uart_buf));
						uint32_t tTimeout = HAL_GetTick();
						while ((HAL_GetTick() - tTimeout) < 500) {
								uint8_t ch;
								if (HAL_UART_Receive(&huart1, &ch, 1, 50) == HAL_OK) {
										if (ch == '\n' || uart_buf_idx >= 30) break;
										uart_buf[uart_buf_idx++] = (char)ch;
								}
						}
						uart_buf[uart_buf_idx] = '\0';
						strncpy(doctor_name, uart_buf, sizeof(doctor_name) - 1);
						is_surgery_ongoing = 1; // ★ Bat co ca mo

						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("XAC MINH OK!", Font_7x10, White);
						ssd1306_SetCursor(0, 16);
						ssd1306_WriteString("Bat Dau Ca Mo", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
						HAL_Delay(800);

						door2_state = 1;
						door2_timer = HAL_GetTick();
						door2_updated = 0;
				}
				else if (rxData == 'H') {
						// ── MO CUA 2 KHONG CAP NHAT TEN (nhan vien vao/ra khi dang mo) ──
						if (door2_state == 0) {
								// Chi mo cua, khong thay doi doctor_name hay OLED
								ssd1306_Fill(Black);
								ssd1306_SetCursor(0, 0);
								ssd1306_WriteString("Nhan Vien Vao", Font_7x10, White);
								ssd1306_SetCursor(0, 14);
								ssd1306_WriteString("Mo Cua...", Font_7x10, White);
								ssd1306_UpdateScreen(&hi2c1);
								door2_state = 1;
								door2_timer = HAL_GetTick();
								door2_updated = 0;
						}
						// Neu cua dang mo/dong thi khong lam gi (cua da xu ly)
				}
				else if (rxData == 'C') {
						// ── HOAN THANH CA MO: Y ta bam tu Web -> reset toan bo ──
						is_surgery_ongoing = 0;
						memset(doctor_name, 0, sizeof(doctor_name));
						ir_approach_sent = 0;

						// Hien thi thong bao hoan thanh
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CA MO HOAN THANH", Font_7x10, White);
						ssd1306_SetCursor(0, 16);
						ssd1306_WriteString("Phong Da Trong", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
						HAL_Delay(2000);

						// OLED tro ve trang cho ban dau
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CUA PHONG MO", Font_7x10, White);
						ssd1306_SetCursor(0, 14);
						ssd1306_WriteString("Vui Long Dua", Font_7x10, White);
						ssd1306_SetCursor(0, 26);
						ssd1306_WriteString("Khuon Mat", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);

						// Neu cua dang mo -> buoc dong lai
						if (door2_state == 2) {
								door2_state = 3;
								door2_timer = HAL_GetTick();
								door2_updated = 0;
						}
				}
				else if (rxData == 'N') {
						// Face ID that bai
						ir_approach_sent = 0;
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("XAC MINH THAT", Font_7x10, White);
						ssd1306_SetCursor(0, 14);
						ssd1306_WriteString("BAI!", Font_7x10, White);
						ssd1306_SetCursor(0, 28);
						ssd1306_WriteString("Vui Long Thu Lai", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
						HAL_Delay(2000);
						ssd1306_Fill(Black);
						ssd1306_SetCursor(0, 0);
						ssd1306_WriteString("CUA PHONG MO", Font_7x10, White);
						ssd1306_SetCursor(0, 14);
						ssd1306_WriteString("Vui Long Dua", Font_7x10, White);
						ssd1306_SetCursor(0, 26);
						ssd1306_WriteString("Khuon Mat", Font_7x10, White);
						ssd1306_UpdateScreen(&hi2c1);
				}
				else if (rxData == 'P') { ESP_SendStatus('A'); }
		}
		
		if(trang_thai == 1){
				if (HAL_GetTick() - trang_thai_cho > 10000) /* Timeout 10s khong nhan phim -> tro ve menu */
							{
									input_idx = 0;
									trang_thai = 0;
									menu();
							}
		}
		
		char key = Keypad_Read(&myKeypad);
		if (key != 0) {
				trang_thai_cho = HAL_GetTick();
				/* ==============================================
				   LOP 1: Nhap mat khau
				   - Chua nhap gi thi bat dau nhap ngay
				   - Moi phim so duoc tu dong them vao
				   ============================================== */
				if (trang_thai == 0) {
						/* Bat ky phim so nao -> bat dau nhap mat khau */
						if (key >= '0' && key <= '9') {
								trang_thai = 1;
								input_idx = 0;
								lcd_clear(&lcd1);
								lcd_gotoxy(&lcd1, 0, 0);
								lcd_puts(&lcd1, "Nhap mat khau:");
								/* Them phim vua nhan vao */
								input_pass[input_idx++] = key;
								lcd_gotoxy(&lcd1, 0, 1);
								lcd_putchar(&lcd1, '*');
						}
				}
				else if (trang_thai == 1) {
				if (key == '#') {
						input_pass[input_idx] = '\0';
						if (strcmp(input_pass, pass) == 0) {
							/* === LOP 1 DUNG: Mo Cua 1 ngay, sau do Lop 2 === */
							trang_thai = 0;
							input_idx  = 0;
							sai_pass   = 0;
							lcd_clear(&lcd1);
							lcd_gotoxy(&lcd1, 0, 0);
							lcd_puts(&lcd1, "Mat khau DUNG!");
							lcd_gotoxy(&lcd1, 0, 1);
							lcd_puts(&lcd1, ">> Mo Cua 1...");
							HAL_Delay(800);
							HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); /* Bat den */
							ESP_SendStatus('U'); /* Bao ESP32 mo cua 1 */
							door_lcd_updated = 0;
							door_state = 1; /* Bat dau mo cua 1 */
							door_timer = HAL_GetTick();
							/* Sau khi cua 1 dong xong, pending_layer2=1 -> Lop 2 */
						} else {
							sai_pass++;
							lcd_clear(&lcd1);
							lcd_puts(&lcd1, "Sai mat khau!");
							HAL_Delay(1000);
							if (sai_pass >= 3) {
									lcd_clear(&lcd1);
									lcd_puts(&lcd1, "Tam khoa 3s!");
									HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);
									HAL_Delay(3000);
									HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);
									sai_pass = 0;
							}
							lcd_clear(&lcd1);
							lcd_gotoxy(&lcd1, 0, 0);
							lcd_puts(&lcd1, "Nhap mat khau:");
						}
						input_idx = 0;
				}
				else if (key == '*') {
						if (input_idx > 0) {
								input_idx--;
								input_pass[input_idx] = '\0';
								lcd_gotoxy(&lcd1, input_idx, 1);
								lcd_puts(&lcd1, " ");
								lcd_gotoxy(&lcd1, input_idx, 1);
						}
				}
				else if (key >= '0' && key <= '9') {
						if (input_idx < 20) {
								input_pass[input_idx++] = key;
								lcd_gotoxy(&lcd1, input_idx - 1, 1);
								lcd_putchar(&lcd1, '*');
						}
				}
				}
		}			
			
		
} /* while*/

		
	
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1000;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

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

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, buzzer_Pin|keypadA7_Pin|in2_Pin|in1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, keypadB0_Pin|keypadB1_Pin|keypadB10_Pin|in4_Pin
                          |in3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : sw_open_Pin sw_close_Pin */
  GPIO_InitStruct.Pin = sw_open_Pin|sw_close_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : buzzer_Pin keypadA7_Pin in2_Pin in1_Pin */
  GPIO_InitStruct.Pin = buzzer_Pin|keypadA7_Pin|in2_Pin|in1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : keypad_Pin keypadA5_Pin keypadA6_Pin IR_down_Pin */
  GPIO_InitStruct.Pin = keypad_Pin|keypadA5_Pin|keypadA6_Pin|IR_down_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : keypadB0_Pin keypadB1_Pin keypadB10_Pin in4_Pin
                           in3_Pin */
  GPIO_InitStruct.Pin = keypadB0_Pin|keypadB1_Pin|keypadB10_Pin|in4_Pin
                          |in3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : ir_sensor_Pin IR_Up_Pin */
  GPIO_InitStruct.Pin = ir_sensor_Pin|IR_Up_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : sw_closeB12_Pin sw_openB13_Pin */
  GPIO_InitStruct.Pin = sw_closeB12_Pin|sw_openB13_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* (Hàm MX_USART1_UART_Init đã được CubeMX tạo bên trên, không cần định nghĩa lại) */

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
