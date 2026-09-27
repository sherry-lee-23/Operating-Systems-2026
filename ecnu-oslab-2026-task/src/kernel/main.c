#include "arch/mod.h"
#include "lib/mod.h"

    volatile static int started = 0;

    volatile static int sum = 0;

    static spinlock_t sum_lock;

    int main()
    {
        int cpuid = r_tp();
        if(cpuid == 0) {
            print_init();
            spinlock_init(&sum_lock, "sum");
            printf("cpu %d is booting!\n", cpuid);        
            __sync_synchronize();
            started = 1;
            spinlock_acquire(&sum_lock);
            for(int i = 0; i < 1000000; i++)
                sum++;
            spinlock_release(&sum_lock);
            printf("cpu %d report: sum = %d\n", cpuid, sum);
        } else {
            while(started == 0);
            __sync_synchronize();
            printf("cpu %d is booting!\n", cpuid);
            spinlock_acquire(&sum_lock);
            for(int i = 0; i < 1000000; i++)
                sum++;
            spinlock_release(&sum_lock);
            printf("cpu %d report: sum = %d\n", cpuid, sum);
        }   
        while (1);    
    }  