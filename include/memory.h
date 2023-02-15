#ifndef APPLE_II_EMULATOR_MEMORY_H
#define APPLE_II_EMULATOR_MEMORY_H

#include <pthread.h>

extern unsigned char memory[0x10000];
extern pthread_mutex_t memMutex;
extern pthread_mutex_t screenMutex;
extern pthread_mutex_t runningMutex;

extern unsigned char readByte(unsigned short address);
extern void writeByte(unsigned short address, unsigned char byte);

#endif //APPLE_II_EMULATOR_MEMORY_H
