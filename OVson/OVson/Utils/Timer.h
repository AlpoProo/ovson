#pragma once
#include <Windows.h>

class TimeUtil {
public:
    static void init() {
        QueryPerformanceFrequency(&s_freq);
    }
    
    static double getTime() {
        if (s_freq.QuadPart == 0) init();
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return (double)li.QuadPart / (double)s_freq.QuadPart;
    }
    
    static float getDelta() {
        double now = getTime();
        if (s_lastTime == 0.0) {
            s_lastTime = now;
            return 0.0f;
        }
        float dt = (float)(now - s_lastTime);
        s_lastTime = now;
        return dt;
    }

private:
   static LARGE_INTEGER s_freq;
   static double s_lastTime;
};