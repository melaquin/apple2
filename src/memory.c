#include "memory.h"
#include "HIF.h"
#include "cassette.h"
#include "peripheral.h"
#include <stdio.h>
#include <string.h>

unsigned char memory[0x10000];

// for accessing memory
pthread_mutex_t memMutex = PTHREAD_MUTEX_INITIALIZER;
// for telling the GLFW thread that the contents of one of the flags has changed
pthread_mutex_t screenMutex = PTHREAD_MUTEX_INITIALIZER;
// for telling the GLFW thread if the CPU is running or not
pthread_mutex_t runningMutex = PTHREAD_MUTEX_INITIALIZER;

// which peripheral card's XROM to access based on the steps detailed in peripheral cards.md
static int selectedXROM = 0;

/**
 * interacts with hardware components if memory-mapped IO has been referenced
 *
 * emulates the function of the I/O selector
 */
static void ioSelect(unsigned short address)
{
    if(address >= 0xC010 && address < 0xD000)
    {
        if(address < 0xC020)
        {
            // clear keyboard strobe
            memset(memory + 0xC000, memory[0xC000] & 0b01111111, 16);
        }
        else if(address < 0xC030)
        {
            record();
            // cassette output toggle
        }
        else if(address < 0xC040)
        {
            // speaker toggle
        }
        else if(address < 0xC050)
        {
            // utility strobe
        }
        else if(address < 0xC058)
        {
            switch(address)
            {
            case 0xC050: // set graphics mode
                pthread_mutex_lock(&screenMutex);
                screenFlagsChanged = 1;
                screenFlags |= gr;
                pthread_mutex_unlock(&screenMutex);
                break;
            case 0xC051: // set text mode
                pthread_mutex_lock(&screenMutex);
                screenFlagsChanged = 1;
                screenFlags &= ~gr;
                pthread_mutex_unlock(&screenMutex);
                break;
            case 0xC052: // set all text or graphics
                screenFlags &= ~mix;
                break;
            case 0xC053: // mix text and graphics
                screenFlags |= mix;
                break;
            case 0xC054: // display primary page
                screenFlags |= pri;
                break;
            case 0xC055: // display secondary page
                screenFlags &= ~pri;
                break;
            case 0xC056: // set low-res graphics
                pthread_mutex_lock(&screenMutex);
                screenFlagsChanged = 1;
                screenFlags |= lores;
                pthread_mutex_unlock(&screenMutex);
                break;
            case 0xC057: // set hi-res graphics
                pthread_mutex_lock(&screenMutex);
                screenFlagsChanged = 1;
                screenFlags &= ~lores;
                pthread_mutex_unlock(&screenMutex);
                break;
            default:
                break;
            }
        }
        else if(address == 0xC060 || address == 0xC068)
        {
            playback();
        }
        // will probably never need to be implemented
/*        else if(address < 0xC060)
        {
            switch(address)
            {
            case 0xC058:
            case 0xC059: // annunciator 0

                break;
            case 0xC05A:
            case 0xC05B: // annunciator 1

                break;
            case 0xC05C:
            case 0xC05D: // annunciator 2

                break;
            case 0xC05E:
            case 0xC05F: // annunciator 3

                break;
            default:
                break;
            }
        }
        else if(address < 0xC070)
        {
            switch(address)
            {
            case 0xC060:
            case 0xC068: // cassette input
                playback();
                break;
            case 0xC061:
            case 0xC069: // pushbutton 1

                break;
            case 0xC062:
            case 0xC06A: // pushbutton 2

                break;
            case 0xC063:
            case 0xC06B: // pushbutton 3

                break;
            case 0xC064:
            case 0xC06C: // game controller input 0

                break;
            case 0xC065:
            case 0xC06D: // game controller input 1

                break;
            case 0xC066:
            case 0xC06E: // game controller input 2

                break;
            case 0xC067:
            case 0xC06F: // game controller input 3

                break;
            default:
                break;
            }
        }
        else if(address < 0xC080)
        {
            // game controller strobe
        }*/
        else if(address >= 0xC080 && address < 0xC100)
        {
            // peripheral card GPIO space, set devSel flag for card or callback if the card is active
            int slot = ((address - 0x80) >> 4) & 0xF;

            if(peripherals[slot].IOStrobe)
            {
                memory[address] = peripherals[slot].deviceSelect(address & 0xF, memory[address]);
            }
        }
        else if(address < 0xC800)
        {
            // if accessing card's PROM space and it's not already active, swap out XROM
            int slot = (address >> 8) & 0xF;
            if(!peripherals[slot].IOStrobe)
            {
                // selectedXROM has an initial value of 0, so since slot 0's XROM is never used, this is fine when activating the very first card
                peripherals[selectedXROM].IOStrobe = 0; // deactivate previous card so it'll pass this check again next time it's used
                memcpy(peripherals[selectedXROM].expansionRom, memory + 0xC800, 0x800);
                memcpy(memory + 0xC800, peripherals[slot].expansionRom, 0x800);
                selectedXROM = slot;
                peripherals[slot].IOStrobe = 1;
            }
        }
    }
}

/**
 * fetches working data from memory according to addressing mode
 *
 * @param address address to read from
 * @return data fetched from memory
 */
unsigned char readByte(unsigned short address)
{
    pthread_mutex_lock(&memMutex);

    ioSelect(address);
    if(peripherals[0].handle != NULL)
    {
        peripherals[0].memRef(address);
    }

    unsigned char ret = memory[address];

    pthread_mutex_unlock(&memMutex);

    return ret;
}

/**
 * stores a byte in memory according to addressing mode
 * @param byte value to store
 */
void writeByte(unsigned short address, unsigned char byte)
{
    pthread_mutex_lock(&memMutex);
    
    // printf("writing %02X to %04X\n", byte, address);
    memory[address] = byte;

    ioSelect(address);
    if(peripherals[0].handle != NULL)
    {
        peripherals[0].memRef(address);
    }

    pthread_mutex_unlock(&memMutex);
}