#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <dlfcn.h>
#include "memory.h"
#include "peripheral.h"
#include "json.h"

struct peripheralCard peripherals[8] = { 0 };
void (*diskDoor)(int) = NULL;

/**
 * reads the config file and loads peripheral cards for use
 * @return 0 = failure, 1 = success
 */
char mountCards()
{
    static char periphCard[FILENAME_MAX];
    char *json;
    FILE *config = fopen("config.json", "rb");
    if(config)
    {
        fseek(config, 0, SEEK_END);
        size_t fileLength = (size_t) ftell(config);
        fseek(config, 0, SEEK_SET);

        json = malloc(fileLength + 1);

        if(fread(json, 1, fileLength, config) != fileLength)
        {
            sprintf(errorMsg, "Error when reading config.json");
            fclose(config);
            printf("%s\n", errorMsg);
            return 0;
        }
        json[fileLength] = 0;
        fclose(config);
    }
    else
    {
        sprintf(errorMsg, "Error when opening config.json");
        printf("%s\n", errorMsg);
        return 0;
    }

    jsmn_parser p;
    jsmntok_t tokens[19];
    jsmn_init(&p);
    int jsmnResult = jsmn_parse(&p, json, strlen(json), tokens, 19);
    if(jsmnResult < 0)
    {
        sprintf(errorMsg, "Error parsing config.json");
        printf("%s\n", errorMsg);
        return 0;
    }
    if(jsmnResult == 0 || tokens[0].type != JSMN_OBJECT)
    {
        sprintf(errorMsg, "Top level of config.json should be an object.");
        printf("%s\n", errorMsg);
        return 0;
    }

    for(int tok = 1; tok < jsmnResult; tok++)
    {
        if(jsonStart(json, &tokens[tok], "slot "))
        {
            strncpy(periphCard, json + tokens[tok].start, (size_t) (tokens[tok].end - tokens[tok].start));
            json[tokens[tok+1].end] = 0;
            if(!isdigit(json[tokens[tok].end - 1]) ||  strlen(json + tokens[tok+1].start) == 0)
            {
                continue;
            }
            json[tokens[tok].end] = 0;

            memcpy(periphCard, "cards/", 6);
            memcpy(periphCard + 6, json + tokens[tok+1].start, strlen(json + tokens[tok+1].start));
            memcpy(periphCard + 6 + strlen(json + tokens[tok+1].start), ".dll", 5);
            int cardNumber = strtol(json +tokens[tok].end - 1, NULL, 10);
            if(errno == EINVAL)
            {
                sprintf(errorMsg, "Error parsing card number in config.json\n");
                printf("%s\n", errorMsg);
                return 0;
            }
            if(cardNumber > 7)
            {
                sprintf(errorMsg, "Error: found card number > 7 (%d)", cardNumber);
                printf("%s\n", errorMsg);
                return 0;
            }

            peripherals[cardNumber].handle = dlopen(periphCard, RTLD_LAZY | RTLD_LOCAL);

            if(strcmp(periphCard, "cards/Disk-II-Controller.so") == 0)
            {
                // super special case card
                diskDoor = dlsym(peripherals[cardNumber].handle, "diskDoor");
                if(diskDoor == NULL)
                {
                    sprintf(errorMsg, "Could not find diskDoor in %s", periphCard);
                    printf("%s\n", errorMsg);
                    return 0;
                }
                char *directory = dlsym(peripherals[cardNumber].handle, "fileDirectory");
                if(directory == NULL)
                {
                    sprintf(errorMsg, "Could not find fileDirectory in %s", periphCard);
                    printf("%s\n", errorMsg);
                    return 0;
                }
                getcwd(directory, FILENAME_MAX);
                directory[strlen(directory)] = '\\';
            }

            peripherals[cardNumber].startup = dlsym(peripherals[cardNumber].handle, "startup");
            if(peripherals[cardNumber].startup == NULL)
            {
                sprintf(errorMsg, "Could not find startup in %s", periphCard);
                printf("%s\n", errorMsg);
                return 0;
            }
            peripherals[cardNumber].shutdown = dlsym(peripherals[cardNumber].handle, "shutdown");
            if(peripherals[cardNumber].shutdown == NULL)
            {
                sprintf(errorMsg, "Could not find shutdown in %s", periphCard);
                printf("%s\n", errorMsg);
                return 0;
            }
            peripherals[cardNumber].deviceSelect = dlsym(peripherals[cardNumber].handle, "deviceSelect");
            if(peripherals[cardNumber].deviceSelect == NULL)
            {
                sprintf(errorMsg, "Could not find deviceSelect in %s", periphCard);
                printf("%s\n", errorMsg);
                return 0;
            }

            if(cardNumber == 0)
            {
                peripherals[cardNumber].memRef = dlsym(peripherals[cardNumber].handle, "memRef");
                if(peripherals[cardNumber].memRef == NULL)
                {
                    sprintf(errorMsg, "Could not find memRef in %s", periphCard);
                    printf("%s\n", errorMsg);
                    return 0;
                }
                // startup takes pointer to memory in this case
                ((void (*) (unsigned char *)) peripherals[cardNumber].startup)(memory);
            }
            else
            {
                memcpy(periphCard + 6 + strlen(json + tokens[tok+1].start), "PROM.bin", 9);
                FILE *promFile = fopen(periphCard, "rb");
                if(promFile)
                {
                    fseek(promFile, 0, SEEK_END);
                    size_t fileLength = (size_t) ftell(promFile);
                    fseek(promFile, 0, SEEK_SET);

                    if(fileLength > 0x100)
                    {
                        sprintf(errorMsg, "PROM file %s is larger than 256 bytes", periphCard);
                        printf("%s\n", errorMsg);
                    }

                    if(fread(memory + 0xC000 + (0x100 * cardNumber), 1, fileLength, promFile) != fileLength)
                    {
                        sprintf(errorMsg, "Error when reading %s", periphCard);
                        printf("%s\n", errorMsg);
                        fclose(promFile);
                        return 0;
                    }
                    fclose(promFile);
                }
                else
                {
                    sprintf(errorMsg, "Error when opening %s", periphCard);
                    printf("%s\n", errorMsg);
                    return 0;
                }
                memcpy(periphCard + 6 + strlen(json + tokens[tok+1].start), "XROM.bin", 9);
                FILE *xromFile = fopen(periphCard, "rb");
                if(xromFile)
                {
                    fseek(xromFile, 0, SEEK_END);
                    size_t fileLength = (size_t) ftell(xromFile);
                    fseek(xromFile, 0, SEEK_SET);

                    if(fileLength > 0x800)
                    {
                        sprintf(errorMsg, "XROM file %s is larger than 2048 bytes", periphCard);
                        printf("%s\n", errorMsg);
                    }

                    if(fread(peripherals[cardNumber].expansionRom, 1, fileLength, xromFile) != fileLength)
                    {
                        sprintf(errorMsg, "Error when reading %s", periphCard);
                        printf("%s\n", errorMsg);
                        fclose(xromFile);
                        return 0;
                    }
                    fclose(xromFile);
                }
                else
                {
                    sprintf(errorMsg, "Error when opening %s", periphCard);
                    printf("%s\n", errorMsg);
                    return 0;
                }

                // startup does not take pointer to memory if not slot 0
                ((void (*)()) peripherals[cardNumber].startup)();
            }
        }
    }
    return 1;
}

/**
 * unmounts all peripheral cards and shuts them down
 */
void unmountCards()
{
    for(int card = 0; card < 8; card++)
    {
        if(peripherals[card].handle != NULL)
        {
            peripherals[card].shutdown();
        }
    }
    // second loop because same card (dll) could be mounted to multiple slots, so we shutdown all of them before dlclosing them
    for(int card = 0; card < 8; card++)
    {
        if(peripherals[card].handle != NULL)
        {
            dlclose(peripherals[card].handle);
            peripherals[card].handle = NULL;
        }
    }
}