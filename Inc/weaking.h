#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {int pwm; int weak;} RetValWeak;
typedef RetValWeak (*WeakingPtr)(int torque, unsigned int period, unsigned int cur_phase, int current);
typedef struct {WeakingPtr func; const char* name; unsigned int cur_limit;} WeakStruct;
RetValWeak nullFuncWeak(int pwm, unsigned int period, unsigned int cur_phase, int current);
extern const WeakStruct weakfunctions[];
void set_weaking(int x);
const char* get_weaking_name(int x);

typedef bool (*TimingPtr)(int lst_phase, int cur_phase, int throttle);
bool no_timing(int lst_phase, int cur_phase, int throttle);
