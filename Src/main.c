/*
* This file is part of the hoverboard-firmware-hack project.
*
* Copyright (C) 2017-2018 Rene Hopf <renehopf@mac.com>
* Copyright (C) 2017-2018 Nico Stute <crinq@crinq.de>
* Copyright (C) 2017-2018 Niklas Fauth <niklas.fauth@kit.fail>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"  // main code for stm32 controller
#include "defines.h"  // for the macros
#include "setup.h"  // for access the functions form setup
#include "bldc.h"  // for the main control variables
#include "buzzertones.h"
#include "hd44780.h"  // for the display
#include "config.h"  // the config
#include "comms.h"
#include "control.h"
#include "generic_device.h"

void SystemClock_Config(void);

LCD_PCF8574_HandleTypeDef lcd;

#ifdef CONTROL_PPM
extern volatile uint16_t ppm_captured_value[PPM_NUM_CHANNELS+1];
#endif



//rollbrett
int divisor = 2;
void device_specific(){
    int turn = (adc_buffer.l_rx2 - ADC_MID) / 8;
    int speed = (adc_buffer.l_tx2 - ADC_MID) / (2 * divisor);

    if (ABS(turn) < 4) {
      turn = 0;
    } else {
      turn -= 4 * SIGN(turn);
    }

    if (ABS(speed) < 5) {
      speed = 0;
    }

    set_throttle(speed + turn, speed - turn);
      // (adc_buffer.l_tx2-ADC_MID) / 2 + (adc_buffer.l_rx2-ADC_MID) / 2,
      // (adc_buffer.l_tx2-ADC_MID) / 2 - (adc_buffer.l_rx2-ADC_MID) / 2);
}

void device_init(){
  set_weaking(2);
}

void device_button(){
  if(divisor == 1)
    divisor = 2;
  else
    divisor = 1;
}

//for linking boost ups buildtime
const uint32_t lowBattery[] = {
  BATTERY_VOLTAGE2ADC12(BAT_LOW_DEAD),
  BATTERY_VOLTAGE2ADC12(BAT_LOW_LVL2),
  BATTERY_VOLTAGE2ADC12(BAT_LOW_LVL1)
};

const uint32_t lowBattery_length = 3;



void init(){
    HAL_Init();
    __HAL_RCC_AFIO_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    /* System interrupt init*/
    /* MemoryManagement_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);
    /* BusFault_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
    /* UsageFault_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
    /* SVCall_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(SVCall_IRQn, 0, 0);
    /* DebugMonitor_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DebugMonitor_IRQn, 0, 0);
    /* PendSV_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(PendSV_IRQn, 0, 0);
    /* SysTick_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

    SystemClock_Config();

    __HAL_RCC_DMA1_CLK_DISABLE();
    MX_GPIO_Init();
    MX_TIM_Init();
    MX_ADC1_Init();
    MX_ADC2_Init();

    #if defined(DEBUG_SERIAL_USART2) || defined(DEBUG_SERIAL_USART3)
      UART_Init();
    #endif

    HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, 1);  // set latch to on

    HAL_ADC_Start(&hadc1);
    HAL_ADC_Start(&hadc2);

    set_buzzer(startUpSound);

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, 1);

    #ifdef CONTROL_PPM
      PPM_Init();
    #endif

      I2C_Init();
      HAL_Delay(50);
      lcd.pcf8574.PCF_I2C_ADDRESS = 0x27;
        lcd.pcf8574.PCF_I2C_TIMEOUT = 5;
        lcd.pcf8574.i2c = hi2c2;
        lcd.NUMBER_OF_LINES = NUMBER_OF_LINES_2;
        lcd.type = TYPE0;

        if(LCD_Init(&lcd)!=LCD_OK){
            // error occured
            //TODO while(1);
        }

      LCD_ClearDisplay(&lcd);
      HAL_Delay(5);
      LCD_SetLocation(&lcd, 0, 0);
      LCD_WriteString(&lcd, "Hover V2.0");
      LCD_SetLocation(&lcd, 0, 1);
      LCD_WriteString(&lcd, "Initializing...");

    load_eeprom();  // initialize variables from eeprom or initialize them
    while(HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN));  // wait for button release
}

#ifndef OVERRIDE_MAIN
int main(void) {
main_start:  // only for defect boards if you think your hardware is working please remove
  init();
  device_init();
  set_bldc_motors(true);
  while(1) {
    HAL_Delay(3);
    device_specific();
    //START FINAL CODE
    if (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN)) {  // turnoff mechanism
      bool btn_release = false;
      unsigned long startTime = get_mainCounter();
      set_bldc_motors(false);
      while(get_mainCounter() < (startTime + (PWM_FREQ * 2))){  // check button for 2s for release to only turn off if its pressed for 2 secs
        if(!HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN)){  // if button released
          btn_release = true;
          break;
        }
      }
      if(btn_release){
        set_buzzer(buttonRelease);
        set_bldc_motors(true);
        // do something
        device_button();
      }
      else{
        set_buzzer(shutDownSound);
        while (get_mainCounter() < (startTime + (PWM_FREQ * 10))) {
          if(!HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN)){
            btn_release = true;
            break;
          }
        }  // wait for button release to turn off
        if(btn_release) {
          turnOff();
          break;
        } else {
          set_buzzer(resetSound);
          while(HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN));
          turnOffWithReset();
          break;
        }
      }
    }

    //END FINAL CODE
    if  (battery_voltage < lowBattery[0]) {
      set_buzzer(lowBatTones[0]);
      set_bldc_motors(false);
      HAL_Delay(200);
      turnOff();
      break;
    }
    for(uint8_t x=1;x<lowBattery_length;x++)
      if  (battery_voltage < BATTERY_VOLTAGE2ADC12(BAT_LOW_LVL2)) {
        set_buzzer(lowBatTones[x]);
        LIMIT_CURRENT(5 + 5*x);  // limiting the motorcurrent to a lower value
        break;
      }
  }
  goto main_start;  // if this goto is used the board is defect and everybody knows: "Defect boards are liking defect code" :D
}
#endif

inline void swp(int* x,int* y){
	int tmp = *x;
	*x = *y;
	*y = tmp;
}
inline void sort_array(int x[],int cnt){
  for(int y=0;y<cnt-1; y++)
		for(int z=y+1;z<cnt; z++)
			if(x[y]>x[z])
				swp(&x[y],&x[z]);
}
int calc_median(int x[],int cnt){
  sort_array(x,cnt);
	if(cnt%2)
		return x[cnt/2];
	else
		return (x[cnt/2]+x[cnt/2+1])/2;
}
/** System Clock Configuration
*/
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL          = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV8;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

  /**Configure the Systick interrupt time
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

  /**Configure the Systick
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}
