#include <stdio.h>
#include <math.h>
#include <time.h>

int main() {
    volatile double x = 0.0;
    while (1) {
        for (int i = 0; i < 1000000; i++)
            x += sqrt((double)i);
        if (x > 1e12) x = 0.0;
    }
    return 0;
}

