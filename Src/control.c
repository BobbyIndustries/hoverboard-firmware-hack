
#include "stm32f1xx_hal.h"
#include "defines.h"
#include "setup.h"
#include "control.h"
#include "config.h"
#include <stdbool.h>
#include <string.h>

TIM_HandleTypeDef TimHandle;
uint8_t ppm_count = 0;
uint32_t timeout = 100;
uint8_t nunchuck_data[6] = {0};

uint8_t i2cBuffer[2];

DMA_HandleTypeDef hdma_i2c2_rx;
DMA_HandleTypeDef hdma_i2c2_tx;

#ifdef CONTROL_PPM
uint16_t ppm_captured_value[PPM_NUM_CHANNELS + 1] = {500, 500};
uint16_t ppm_captured_value_buffer[PPM_NUM_CHANNELS+1] = {500, 500};
uint32_t ppm_timeout = 0;

bool ppm_valid = true;

#define IN_RANGE(x, low, up) (((x) >= (low)) && ((x) <= (up)))

void PPM_ISR_Callback() {
  // Dummy loop with 16 bit count wrap around
  uint16_t rc_delay = TIM2->CNT;
  TIM2->CNT = 0;

  if (rc_delay > 3000) {
    if (ppm_valid && ppm_count == PPM_NUM_CHANNELS) {
      ppm_timeout = 0;
      memcpy(ppm_captured_value, ppm_captured_value_buffer, sizeof(ppm_captured_value));
    }
    ppm_valid = true;
    ppm_count = 0;
  }
  else if (ppm_count < PPM_NUM_CHANNELS && IN_RANGE(rc_delay, 900, 2100)){
    timeout = 0;
    ppm_captured_value_buffer[ppm_count++] = CLAMP(rc_delay, 1000, 2000) - 1000;
  } else {
    ppm_valid = false;
  }
}

// SysTick executes once each ms
void PPM_SysTick_Callback() {
  ppm_timeout++;
  // Stop after 500 ms without PPM signal
  if(ppm_timeout > 500) {
    int i;
    for(i = 0; i < PPM_NUM_CHANNELS; i++) {
      ppm_captured_value[i] = 500;
    }
    ppm_timeout = 0;
  }
}

void PPM_Init() {
  GPIO_InitTypeDef GPIO_InitStruct;
  /*Configure GPIO pin : PA3 */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  __HAL_RCC_TIM2_CLK_ENABLE();
  TimHandle.Instance = TIM2;
  TimHandle.Init.Period = UINT16_MAX;
  TimHandle.Init.Prescaler = (SystemCoreClock/DELAY_TIM_FREQUENCY_US)-1;;
  TimHandle.Init.ClockDivision = 0;
  TimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
  HAL_TIM_Base_Init(&TimHandle);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);
  HAL_TIM_Base_Start(&TimHandle);
}
#endif

void Nunchuck_Init() {
    //-- START -- init WiiNunchuck
  i2cBuffer[0] = 0xF0;
  i2cBuffer[1] = 0x55;

  HAL_I2C_Master_Transmit(&hi2c2,0xA4,(uint8_t*)i2cBuffer, 2, 100);
  HAL_Delay(10);

  i2cBuffer[0] = 0xFB;
  i2cBuffer[1] = 0x00;

  HAL_I2C_Master_Transmit(&hi2c2,0xA4,(uint8_t*)i2cBuffer, 2, 100);
  HAL_Delay(10);
}

void Nunchuck_Read() {
  i2cBuffer[0] = 0x00;
  HAL_I2C_Master_Transmit(&hi2c2,0xA4,(uint8_t*)i2cBuffer, 1, 100);
  HAL_Delay(5);
  if (HAL_I2C_Master_Receive(&hi2c2,0xA4,(uint8_t*)nunchuck_data, 6, 100) == HAL_OK) {
    timeout = 0;
  } else {
    timeout++;
  }

  if (timeout > 3) {
    HAL_Delay(50);
    Nunchuck_Init();
  }

  //setScopeChannel(0, (int)nunchuck_data[0]);
  //setScopeChannel(1, (int)nunchuck_data[1]);
  //setScopeChannel(2, (int)nunchuck_data[5] & 1);
  //setScopeChannel(3, ((int)nunchuck_data[5] >> 1) & 1);
}

int clean_adc(int inval){
	if((inval & 0x3FF)-DEAD_ZONE-ADC_MIN<0)
    return PWM_MIN;  // if ival in under deadzone
	else if((inval & 0x3FF)>ADC_MAX-2*DEAD_ZONE)
    return PWM_MAX;  // if ival in upper deadzone
  else
	  return (((inval & 0x3FF) - DEAD_ZONE - ADC_MIN) * (PWM_MAX - PWM_MIN)) / (ADC_MAX - ADC_MIN) + PWM_MIN;  // Map value linear to area in PWM area
}

#define WHEELBASE 2
#define WHEEL_WIDTH 1
#define STEERING_TO_WHEEL_DIST 1
inline void calc_torque_per_wheel(int throttle, float steering_eagle, int* torque){
  int back_wheel = WHEELBASE / tan(abs(steering_eagle));
  int radius_main = sqrt(pow(back_wheel, 2)+pow(WHEELBASE / 2 ,2));
#if !defined(STEERING)
  torque[0] = (back_wheel + WHEEL_WIDTH/2 * sign(steering_eagle)) * throttle / radius_main;
  torque[1] = (back_wheel - WHEEL_WIDTH/2 * sign(steering_eagle)) * throttle / radius_main;
#else
  #define wheel_bl (back_wheel + (WHEEL_WIDTH/2 * sign(steering_eagle) - STEERING_TO_WHEEL_DIST))
  #define wheel_br (back_wheel - (WHEEL_WIDTH/2 * sign(steering_eagle) - STEERING_TO_WHEEL_DIST))
  torque[0] = (sqrt(pow(wheel_bl, 2)+pow(WHEELBASE,2)) + STEERING_TO_WHEEL_DIST * sign(steering_eagle)) * throttle / radius_main;
  torque[1] = (sqrt(pow(wheel_br, 2)+pow(WHEELBASE,2)) - STEERING_TO_WHEEL_DIST * sign(steering_eagle)) * throttle / radius_main;
  #undef wheel_bl
  #undef wheel_br
#endif
}
int calc_torque(int throttle,int breaks){
  if(breaks == 0){  // drive forward
    return throttle;
}
  else if(breaks == PWM_MAX){  // drive backwards
    return -throttle
  }
  else{
    return = 0;
  }

}
