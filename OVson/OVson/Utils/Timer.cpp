#include "Timer.h"

LARGE_INTEGER TimeUtil::s_freq = {0};
double TimeUtil::s_lastTime = 0.0;
