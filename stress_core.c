#include <stdio.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

int main() {
    volatile double x = 0.0;
    time_t cur_time;
    time(&cur_time);
    while (1) {
        time_t now;
        time(&now);
        if (now - cur_time >= 5){
            sleep(5);
            time(&cur_time);
        }
        for (int i = 0; i < 1000000; i++)
            x += sqrt((double)i);
        if (x > 1e12) x = 0.0;
    }
    return 0;
}

