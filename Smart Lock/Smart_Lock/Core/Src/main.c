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
#include "MFRC522.h"
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

#define IR_Pin GPIO_PIN_11
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
//SPI_HandleTypeDef hspi2; // Dummy hspi2 de MFRC522.c bien dich duoc (Neu muon dung the tu, ban can enable SPI2 trong CubeMX!)
I2C_HandleTypeDef hi2c1; /* Thêm lại I2C handle cho LCD */
I2C_LCD_HandleTypeDef lcd1;

uint8_t door_state = 0;
uint32_t door_timer = 0;
uint8_t door_lcd_updated = 0; // Co cap nhat LCD theo tung state

uint8_t rxData;
uint8_t stt;
char pass[] = "1234"; 
char input_pass[21];
int input_idx = 0;
int trang_thai = 0;
uint32_t trang_thai_cho;

uint16_t sai_pass = 0;

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
/* USER CODE BEGIN PFP */
static void MX_USART1_UART_Init(void);
void ESP_SendStatus(uint8_t c) { HAL_UART_Transmit(&huart1, &c, 1, 100); }
void menu(){
    lcd_clear(&lcd1);
    
    lcd_gotoxy(&lcd1, 0, 0);
    lcd_puts(&lcd1, "1. Nhap mat khau");
    
    lcd_gotoxy(&lcd1, 0, 1);
    lcd_puts(&lcd1, "2. Quet dinh danh");
    
    lcd_gotoxy(&lcd1, 0, 2);
    lcd_puts(&lcd1, "3. Thiet lap"); // Sửa lại số thứ tự
    
    lcd_gotoxy(&lcd1, 0, 3);
    lcd_puts(&lcd1, "4. Thoat");
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
  /* USER CODE BEGIN 2 */
		HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
		__HAL_TIM_MOE_ENABLE(&htim1); // BAT BUOC phai co de xuat PWM tren Timer 1 (Advanced Timer)
		
		lcd1.hi2c = &hi2c1;
    lcd1.address = 0x4E;
    lcd_init(&lcd1);

    menu();
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
		ESP_SendStatus('A');

		/* Cau hinh tuong minh CTHT: PA1=mo, PA2=dong (PULLUP, NO type)
		 * Switch chua cham: HIGH (GPIO_PIN_SET)
		 * Thanh cua cham switch: LOW  (GPIO_PIN_RESET)  */
		{
				GPIO_InitTypeDef sw_cfg = {0};
				sw_cfg.Pin  = GPIO_PIN_1 | GPIO_PIN_2; // PA1=CTHT mo, PA2=CTHT dong
				sw_cfg.Mode = GPIO_MODE_INPUT;
				sw_cfg.Pull = GPIO_PULLUP;
				HAL_GPIO_Init(GPIOA, &sw_cfg);
		}
  /* USER CODE END 2 */


  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	
	
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
		// =====================================================
		// DOOR STATE MACHINE (Limit Switch Based)
		//   State 0: Cua dong (nghỉ)
		//   State 1: Dang mo  → PWM đến khi sw_open (PA1) active
		//   State 2: Cua da mo → chờ IR clear 5s rồi đóng
		//   State 3: Dang dong → PWM đến khi sw_close (PA2) active
		//            hoặc IR phat hiện → mở lại
		// Limit switch: PULLUP, chạm vào → GPIO_PIN_RESET
		// IR sensor:    PULLUP, có người  → GPIO_PIN_RESET
		// =====================================================

		if (door_state == 1) { // ----- DANG MO CUA -----
				if (door_lcd_updated != 1) {
						door_lcd_updated = 1;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, ">> DANG MO CUA");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Dong co dang quay");
				}
				// Cap PWM 100%, chieu mo cua (IN1=1, IN2=0)
				__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 999);
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);

				// Kiem tra PA1: Neu len muc CAO (SET) thi la cham cong tac. Hoac neu kẹt vuot qua 15s thi tu dung.
				if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET || (HAL_GetTick() - door_timer > 15000)) {
						// Dung dong co
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
						door_state = 2;       // Cua da mo hoan toan
						door_timer = HAL_GetTick(); // Bat dau dem 5s IR
						door_lcd_updated = 0;
				}

		} else if (door_state == 2) { // ----- CUA DA MO, CHO IR -----
				if (door_lcd_updated != 2) {
						door_lcd_updated = 2;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "CUA DA MO");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Cho nguoi di vao...");
				}
				// IR phat hien vat the (active LOW) → reset timer, giu cua mo
				if (HAL_GPIO_ReadPin(GPIOB, IR_Pin) == GPIO_PIN_RESET) {
						door_timer = HAL_GetTick();
				}
				// Khong phat hien trong 5s → bat dau dong cua
				if (HAL_GetTick() - door_timer > 5000) {
						door_state = 3;
						door_timer = HAL_GetTick();
						door_lcd_updated = 0;
				}

		} else if (door_state == 3) { // ----- DANG DONG CUA -----
				if (door_lcd_updated != 3) {
						door_lcd_updated = 3;
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, ">> DANG DONG CUA");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, "Dong co dang quay");
				}

				// IR phat hien nguoi tien lai gan khi dang dong → MO LAI ngay
				if (HAL_GPIO_ReadPin(GPIOB, IR_Pin) == GPIO_PIN_RESET) {
						// Dung dong co truoc
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
						// Mo cua lai
						door_state = 1;
						door_timer = HAL_GetTick();
						door_lcd_updated = 0;
				} else {
						// Cap PWM 100%, chieu dong cua (IN1=0, IN2=1)
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 999);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

						// Kiem tra PA2: Neu len muc CAO (SET) thi la cham cong tac. Hoac neu kẹt vuot qua 15s thi tu dung.
						if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_SET || (HAL_GetTick() - door_timer > 15000)) {


								// Dung dong co
								__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
								HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
								HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
								HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // Tat den
								ESP_SendStatus('L'); // Bao ESP32 da dong
								door_state = 0;
								door_lcd_updated = 0;
								lcd_clear(&lcd1);
								lcd_gotoxy(&lcd1, 0, 0);
								lcd_puts(&lcd1, "CUA DA DONG");
								HAL_Delay(1000);
								menu();
						}
				}
		}

		
		/* Nhan lenh tu ESP32 qua UART1 */
		__HAL_UART_CLEAR_OREFLAG(&huart1); // Clear ORE flag proactively
		if (HAL_UART_Receive(&huart1, &rxData, 1, 5) == HAL_OK) {
				if (rxData == 'O') {
						// Lenh mo cua tu Web/App
						trang_thai = 0;
						input_idx = 0;
						
						lcd_clear(&lcd1);
						lcd_gotoxy(&lcd1, 0, 0);
						lcd_puts(&lcd1, "LENH TU WEB");
						lcd_gotoxy(&lcd1, 0, 1);
						lcd_puts(&lcd1, ">> Mo khoa...");
						HAL_Delay(800);
						
						HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // Bat den
						ESP_SendStatus('U');
						
						door_lcd_updated = 0;
						door_state = 1;
						door_timer = HAL_GetTick();
						// KHONG goi menu() o day - se tu dong hien LCD theo state
				}
				else if (rxData == 'P') { ESP_SendStatus('A'); }
		}
		
		if(trang_thai == 1){
				if (HAL_GetTick() - trang_thai_cho > 3000)
							{
									input_idx = 0;
									trang_thai = 0;
									menu();
							}
		}
		
		char key = Keypad_Read(&myKeypad);
		if (key != 0) {
				trang_thai_cho = HAL_GetTick();
				if (trang_thai == 0) {
						if (key == '1') {
								trang_thai = 1;
								input_idx = 0;
								lcd_clear(&lcd1);
								lcd_gotoxy(&lcd1, 0, 0);
								lcd_puts(&lcd1, "Nhap mat khau:");
						} 
						else if (key == '2') {
								lcd_clear(&lcd1);
								lcd_puts(&lcd1, "Vui long quet");
								lcd_gotoxy(&lcd1, 1, 0);
								lcd_puts(&lcd1, "the...");

								// TẠM THỜI VÔ HIỆU HOÁ RFID THEO YÊU CẦU
#if 0
								uint32_t startTime = HAL_GetTick(); // L?y th?i di?m b?t d?u
								uint8_t status;
								uint8_t str[5]; // M?ng luu UID th?
								uint8_t found = 0;

								// Ch? qu?t th? trong vng 3 giy (3000ms)
								while ((HAL_GetTick() - startTime) < 10000) {
										// Ki?m tra xem c th? trong vng d?c khng
										status = MFRC522_Request(PICC_REQIDL, str);
										if (status == MI_OK) {
												// N?u tm th?y th?, th?c hi?n ch?ng va ch?m d? l?y UID
												status = MFRC522_Anticoll(str);
												if (status == MI_OK) {
														// Gi? s? m UID th? dng l: 0x12, 0x34, 0x56, 0x78 (Thay b?ng m th? c?a b?n)
														if (str[0] == 0x12 && str[1] == 0x34 && str[2] == 0x56 && str[3] == 0x78) {
																lcd_clear(&lcd1);
																lcd_puts(&lcd1, "The dung!");
																HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // B?t dn
																HAL_Delay(2000);
														} else {
																lcd_clear(&lcd1);
																lcd_puts(&lcd1, "The sai!");
																HAL_Delay(2000);
														}
														found = 1;
														break; // Thot vng l?p while sau khi x? l xong th?
												}
										}
								}

								if (!found) {
										lcd_clear(&lcd1);
										lcd_puts(&lcd1, "Qua thoi gian!");
										HAL_Delay(1000);
								}
#endif
								menu(); // Tr? v? menu
						}
				}


				else if (trang_thai == 1) {
						if (key == '#') {
								input_pass[input_idx] = '\0';
								if (strcmp(input_pass, pass) == 0) {
									/* === LOP 1 DUNG: Yeu cau xac minh khuon mat (Lop 2) === */
									trang_thai = 0;
									input_idx  = 0;
									sai_pass   = 0;
									lcd_clear(&lcd1);
									lcd_puts(&lcd1, "Lop 1: OK!");
									lcd_gotoxy(&lcd1, 0, 1);
									lcd_puts(&lcd1, "Nhin vao camera");
									ESP_SendStatus('F'); /* Yeu cau Face ID tu ESP32 */

									/* Cho ket qua tu ESP32: 'O'=mo, 'X'=khoa tam (toi da 20 giay) */
									uint32_t faceTimeout = HAL_GetTick();
									uint8_t  faceResult  = 0; /* 0=cho, 1=mo, 2=khoa */
									uint8_t  fr;
									__HAL_UART_CLEAR_OREFLAG(&huart1); // Clear ORE to unlock read if necessary
									while ((HAL_GetTick() - faceTimeout) < 20000 && faceResult == 0) {
										if (HAL_UART_Receive(&huart1, &fr, 1, 200) == HAL_OK) {
											if (fr == 'O') faceResult = 1;
											if (fr == 'X') faceResult = 2;
										}
										__HAL_UART_CLEAR_OREFLAG(&huart1); // Xoá lỗi tràn cờ liên tục nếu UART bị tắt nghẽn
									}

									if (faceResult == 1) {
										/* Xac minh thanh cong -> Mo cua */
										lcd_clear(&lcd1);
										lcd_gotoxy(&lcd1, 0, 0);
										lcd_puts(&lcd1, "Xac minh OK!");
										lcd_gotoxy(&lcd1, 0, 1);
										lcd_puts(&lcd1, ">> Mo cua...");
										HAL_Delay(800); // Hien thi thong bao
										
										HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // Bat den
										ESP_SendStatus('U');
										
										door_lcd_updated = 0; // Reset de LCD tu dong cap nhat theo state
										door_state = 1;
										door_timer = HAL_GetTick();
										// KHONG goi menu() - LCD se hien thi theo tung state cua
									} else if (faceResult == 2) {
										/* That bai 3 lan -> Tam khoa 3s */
										lcd_clear(&lcd1);
										lcd_gotoxy(&lcd1, 0, 0);
										lcd_puts(&lcd1, "XM that bai 3x!");
										lcd_gotoxy(&lcd1, 0, 1);
										lcd_puts(&lcd1, "Tam khoa 3s...");
										HAL_Delay(3000);
										menu();
									} else {
										/* Het thoi gian */
										lcd_clear(&lcd1);
										lcd_gotoxy(&lcd1, 0, 0);
										lcd_puts(&lcd1, "Het thoi gian!");
										HAL_Delay(1000);
										menu();
									}
								} else {
									  sai_pass ++;
										lcd_clear(&lcd1);
										lcd_puts(&lcd1, "Sai mat khau");
									  HAL_Delay(1000);
									if(sai_pass >= 3){
										lcd_clear(&lcd1);
										lcd_puts(&lcd1, "Tam khoa 2s");
										HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET );
										HAL_Delay(2000);
										HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET );
										
										sai_pass = 0;					
									}
										lcd_clear(&lcd1);
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
						else {
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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, buzzer_Pin|keypadA7_Pin|in2_Pin|in1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, keypadB0_Pin|keypadB1_Pin|keypadB10_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : sw_open_Pin sw_close_Pin keypad_Pin keypadA5_Pin
                           keypadA6_Pin */
  GPIO_InitStruct.Pin = sw_open_Pin|sw_close_Pin|keypad_Pin|keypadA5_Pin
                          |keypadA6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : buzzer_Pin keypadA7_Pin in2_Pin in1_Pin */
  GPIO_InitStruct.Pin = buzzer_Pin|keypadA7_Pin|in2_Pin|in1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : keypadB0_Pin keypadB1_Pin keypadB10_Pin */
  GPIO_InitStruct.Pin = keypadB0_Pin|keypadB1_Pin|keypadB10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : ir_sensor_Pin */
  GPIO_InitStruct.Pin = ir_sensor_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ir_sensor_GPIO_Port, &GPIO_InitStruct);

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
