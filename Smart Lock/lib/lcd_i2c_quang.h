#ifndef I2C_LCD_H
#define I2C_LCD_H

#include <stdint.h>


#include "main.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;     // I2C handler for communication
    uint8_t address;            // I2C address of the LCD
} I2C_LCD_HandleTypeDef;


void lcd_init(I2C_LCD_HandleTypeDef *lcd);

void lcd_send_cmd(I2C_LCD_HandleTypeDef *lcd, char cmd);

void lcd_send_data(I2C_LCD_HandleTypeDef *lcd, char data);

void lcd_putchar(I2C_LCD_HandleTypeDef *lcd, char ch);


void lcd_puts(I2C_LCD_HandleTypeDef *lcd, char *str);

void lcd_gotoxy(I2C_LCD_HandleTypeDef *lcd, int col, int row);

void lcd_clear(I2C_LCD_HandleTypeDef *lcd);

#endif /* I2C_LCD_H */
