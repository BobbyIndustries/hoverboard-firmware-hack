#include <math.h>
#include <stdlib.h>
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

#define BUFFERSIZE 128
#define VAL_CNT 2

uint32_t index[VAL_CNT];
uint32_t buff_vals[VAL_CNT][BUFFERSIZE];
uint32_t cur_buff_val_sum[VAL_CNT];

uint32_t value_buffer(uint32_t in,int val){
  cur_buff_val_sum[val] -= buff_vals[val][index[val]];
  cur_buff_val_sum[val] += (buff_vals[val][index[val]] = (in >> 16));
  index[val] = (index[val] + 1) % BUFFERSIZE;
  return (cur_buff_val_sum[val] / (BUFFERSIZE)) << 16;
}

void init_debug_screen(){
  for(int i = 0; i < 4; i++)
    for(int j = 0; j < 20; j++)
    Display_show_string(j, i, " "); // empty screen
  Display_show_string(0,0, "Phase:");
  Display_show_string(0,1, "Pos:");
  Display_show_string(0,2, "blockcur:");
  Display_show_string(0,3, "Pwr:");
}
void update_debug_screen(){

}
//bobbycar
int clean_adc_full(uint32_t inval){
  int outval = (uint32_t)(inval >> 16) - ADC_MID;
  if(abs(outval) < (DEAD_ZONE / 2))
    return 0;
  else
    outval -= (DEAD_ZONE / 2) * SIGN(outval);
  if(abs(outval) > (ADC_MAX / 2 - ((DEAD_ZONE * 3) / 2)))
    return THROTTLE_MAX * SIGN(outval);
  return outval * THROTTLE_MAX / (ADC_MAX / 2 - ((DEAD_ZONE*3)/2));
}

int clean_adc_half(uint32_t inval){
  int outval = (uint32_t)(inval >> 16);
  if(abs(outval) > (ADC_MAX - ((DEAD_ZONE * 3) / 2)))
    return THROTTLE_MAX;
  return outval * THROTTLE_MAX / (ADC_MAX - ((DEAD_ZONE*3)/2));
}

int throttle_calc(int cleaned_adc){
  return cleaned_adc < 0 ?
    ((cleaned_adc * cleaned_adc  / THROTTLE_REVERSE_MAX ) * 2 + cleaned_adc ) / 3 :
    ((cleaned_adc * cleaned_adc / THROTTLE_MAX ) * 2 + cleaned_adc ) / 3;
}

int calc_torque(int throttle,int breaks){
  if(breaks == 0){  // drive forward
    return throttle;
}
  else if(breaks == PWM_MAX){  // drive backwards
    return -throttle;
  }
  else{
    return throttle-breaks;
  }
}

float calc_steering_eagle(int inval){
  return (float)inval * STEERING_EAGLE_FACTOR;
}

inline void calc_torque_per_wheel(int throttle, float steering_eagle, int* torque){
  int back_wheel = WHEELBASE / tan(abs(steering_eagle));
  int radius_main = sqrt(pow(back_wheel, 2)+pow(WHEELBASE / 2 ,2));
#if !defined(STEERING)
  torque[0] = (back_wheel + WHEEL_WIDTH/2 * SIGN(steering_eagle)) * throttle / radius_main;
  torque[1] = (back_wheel - WHEEL_WIDTH/2 * SIGN(steering_eagle)) * throttle / radius_main;
#else
  #define wheel_bl (back_wheel + (WHEEL_WIDTH/2 * SIGN(steering_eagle) - STEERING_TO_WHEEL_DIST))
  #define wheel_br (back_wheel - (WHEEL_WIDTH/2 * SIGN(steering_eagle) - STEERING_TO_WHEEL_DIST))
  torque[0] = (sqrt(pow(wheel_bl, 2)+pow(WHEELBASE,2)) + STEERING_TO_WHEEL_DIST * SIGN(steering_eagle)) * throttle / radius_main;
  torque[1] = (sqrt(pow(wheel_br, 2)+pow(WHEELBASE,2)) - STEERING_TO_WHEEL_DIST * SIGN(steering_eagle)) * throttle / radius_main;
  #undef wheel_bl
  #undef wheel_br
#endif
}

void device_specific(){
  int tmp_throttle_per_wheel[2];
  int tmp_throttle;
    calc_torque_per_wheel(
      tmp_throttle = throttle_calc(
        calc_torque(
          clean_adc_half(virtual_ival[0][0]),
          clean_adc_half(virtual_ival[0][1])
        )
      ),
      calc_steering_eagle(clean_adc_full(virtual_ival[1][0])),
      tmp_throttle_per_wheel);
    #ifdef BEEPS_BACKWARD
    if(tmp_throttle < THROTTLE_REVERSE_MAX / 10)
      set_buzzer(reverseSound);
    else
      stop_buzzer();
    #endif
    set_throttle(tmp_throttle_per_wheel[0], tmp_throttle_per_wheel[1]);
  update_debug_screen();
}

void device_init(){
  HAL_Delay(50);
  init_Display(4,0x3F);
  set_weaking(3);
  //PPM_Init();
  for(int i = 0; i < VAL_CNT ; i++){
    cur_buff_val_sum[i] = index[i] = 0;
    for(int j = 0; j < BUFFERSIZE;j++)
      cur_buff_val_sum[i] += (buff_vals[i][j] = ADC_MID);
  }
  init_debug_screen();
}



void device_button(){
  /*if(divisor == 1 && weak){
    divisor = 3;
    weak = false;
    set_weaking(2);
  }
  else if(divisor == 3 && !weak){
    divisor = 2;
    weak = false;
    set_weaking(2);
  }
  else if(divisor == 1 && !weak){
    divisor = 1;
    weak = true;
    set_weaking(3);
  }
  else{
    divisor = 1;
    weak = false;
    set_weaking(2);
  }*/
}

//for linking boost ups buildtime
const uint32_t lowBattery[] = {
  BATTERY_VOLTAGE2ADC12(BAT_LOW_DEAD),
  BATTERY_VOLTAGE2ADC12(BAT_LOW_LVL2),
  BATTERY_VOLTAGE2ADC12(BAT_LOW_LVL1)
};

const uint32_t lowBattery_length = 3;