#include <stdio.h>
#include "cpu.h"
#include "HIF.h"
#include "memory.h"
#include "peripheral.h"
#include "cassette.h"
#include <time.h>

// PSA: refer to these two pages while reading this code:
// https://www.masswerk.at/6502/6502_instruction_set.html instructions list and first of the 3 tables at the bottom
// http://nparker.llx.com/a2/opcodes.html more tables to organize instructions

pthread_t cpuThread;

// registers
static unsigned char A = 0;
static unsigned char X = 0;
static unsigned char Y = 0;
static unsigned char S = 0;
static union
{
    struct
    {
        unsigned char PCLO;
        unsigned char PCHI;
    };
    unsigned short PC;
} pc = { 0 };
static union
{
    struct
    {
        unsigned C : 1;     // carry
        unsigned Z : 1;     // zero
        unsigned I : 1;     // interrupt disable
        unsigned D : 1;     // decimal mode
        unsigned B : 1;     // break command
        unsigned unused : 1;
        unsigned V : 1;     // overflow
        unsigned N : 1;     // negative
    };
    unsigned char P;
} status = { 0 };

#define PC pc.PC
#define PCLO pc.PCLO
#define PCHI pc.PCHI
#define C status.C
#define Z status.Z
#define I status.I
#define D status.D
#define B status.B
#define V status.V
#define N status.N
#define P status.P

struct instruction
{
    unsigned c : 2;
    unsigned b : 3;
    unsigned a : 3;
};

// individual bits of aaa in branch instructions have special meaning
struct branchInstruction
{
    unsigned unused : 5;
    unsigned y : 1;     // compare flag value
    unsigned x : 2;     // flag selector
};

// special case instructions
#define IS_BRANCH_INSTRUCTION(instr) instr.c == 0b00 && instr.b == 0b100
#define IS_JSR(instr) instr.c == 0b00 && instr.b == 0b000 && instr.a == 0b001
#define IS_JMP_ABS(instr) instr.c == 0b00 && instr.b == 0b011 && instr.a == 0b010
#define IS_JMP_IND(instr) instr.c == 0b00 && instr.b == 0b011 && instr.a == 0b011
#define IS_IMPL(instr) (instr.c == 0b00 && (instr.b == 0b000 && instr.a < 0b100 || instr.b == 0b010 || instr.b == 0b110)) || (instr.c == 0b10 && ((instr.b == 0b010 && instr.a >= 0b100) || instr.b == 0b110))

#define ACC     0
#define ABS     1
#define ABSX    2
#define ABSY    3
#define IMME    4
#define IMPL    5
#define XIND    7
#define INDY    8
#define ZPG     10
#define ZPGX    11
#define ZPGY    12

static int addressMode = ACC;

/**
 * pushes a value onto the stack
 * @param val the value to push
 */
static void push(unsigned char val)
{
    memory[0x100 + S--] = val;
}

/**
 * pulls (pops) a value from the stack
 * @return value pulled from the stack
 */
static unsigned char pull()
{
    return memory[0x100 + ++S];
}

static unsigned char load()
{
    unsigned short address = 0;

    switch(addressMode)
    {
    case ACC:       // operand is accumulator, implied
        return A;
    case ABS:       // operand is absolute address
        address = *((unsigned short *) (memory + PC + 1));
        break;
        //return memory[*((unsigned short *) (memory + PC + 1))];
    case ABSX:      // operand is address; effective address is address incremented by X with carry
        address = (unsigned short) (*((unsigned short *) (memory + PC + 1)) + X + C);
        break;
    case ABSY:      // operand is address; effective address is address incremented by Y with carry
        address = (unsigned short) (*((unsigned short *) (memory + PC + 1)) + Y + C);
        break;
    case IMME:      // operand is byte
        address = (unsigned short) (PC + 1);
        break;
    case XIND:      // operand is zeropage address; effective address is word in (LL + X, LL + X + 1), inc. without carry
        address = *((unsigned short *) (memory + memory[PC+1] + X));
        break;
    case INDY:      // operand is zeropage address; effective address is word in (LL, LL + 1) incremented by Y with carry
        address = (unsigned short) (*((unsigned short *) (memory + memory[PC+1])) + Y + C);
        break;
    case ZPG:       // operand is zeropage address
        address = (unsigned short) (memory[PC+1]);
        break;
    case ZPGX:      // operand is zeropage address; effective address is address incremented by X without carry
        address = (unsigned short) (memory[PC+1] + X);
        break;
    case ZPGY:      // operand is zeropage address; effective address is address incremented by Y without carry
        address = (unsigned short) (memory[PC+1] + Y);
        break;
    default:
        return 0;
    }
    return readByte(address);
}

/**
 * stores a byte in memory
 * @param byte
 */
static void store(unsigned char byte)
{
    unsigned short address = 0;

    switch(addressMode)
    {
    case ABS:       // operand is absolute address
        address = *((unsigned short *) (memory + PC + 1));
        break;
        //return memory[*((unsigned short *) (memory + PC + 1))];
    case ABSX:      // operand is address; effective address is address incremented by X with carry
        address = (unsigned short) (*((unsigned short *) (memory + PC + 1)) + X + C);
        break;
    case ABSY:      // operand is address; effective address is address incremented by Y with carry
        address = (unsigned short) (*((unsigned short *) (memory + PC + 1)) + Y + C);
        break;
    case IMME:      // operand is byte
        address = (unsigned short) (PC + 1);
        break;
    case XIND:      // operand is zeropage address; effective address is word in (LL + X, LL + X + 1), inc. without carry
        address = *((unsigned short *) (memory + memory[PC+1] + X));
        break;
    case INDY:      // operand is zeropage address; effective address is word in (LL, LL + 1) incremented by Y with carry
        address = (unsigned short) (*((unsigned short *) (memory + memory[PC+1])) + Y + C);
        break;
    case ZPG:       // operand is zeropage address
        address = (unsigned short) (memory[PC+1]);
        break;
    case ZPGX:      // operand is zeropage address; effective address is address incremented by X without carry
        address = (unsigned short) (memory[PC+1] + X);
        break;
    case ZPGY:      // operand is zeropage address; effective address is address incremented by Y without carry
        address = (unsigned short) (memory[PC+1] + Y);
        break;
    default:
        return;
    }

    writeByte(address, byte);
}

/**
 * executes the instruction pointed to by PC
 * @return 1 for success, 0 for error such as invalid instruction
 */
static int executeInstruction()
{
    unsigned char instr = readByte(PC);
    struct instruction currentInstruction = *(struct instruction *) &instr;

    //printf("PC: %04X\t%02X %02X %02X\n\nA\tX\tY\tS\tN V B D I Z C\n%02X\t%02X\t%02X\t%02X\t%u %u %u %u %u %u %u\n\n", PC, memory[PC], memory[PC+1], memory[PC+2], A, X, Y, S, N, V, B, D, I, Z, C);

    // only halt instruction supported
    if(currentInstruction.a == 0b000 && currentInstruction.b == 0b000 && currentInstruction.c == 0b10)
    {
        pthread_mutex_lock(&runningMutex);
        running = 0;
        pthread_mutex_unlock(&runningMutex);
        return 0;
    }

    // special case instructions first
    if(IS_JSR(currentInstruction))
    {
        // push address of instruction following JSR to return to
        PC += 2;
        push(PCHI);
        push(PCLO);
        // jump to address in argument
        PC = *((unsigned short *) (memory + PC - 1));
    }
    else if(IS_JMP_ABS(currentInstruction))
    {
        PC = *((unsigned short *) (memory + PC + 1));
    }
    else if(IS_JMP_IND(currentInstruction))
    {
        PC = *((unsigned short *) (memory + *((unsigned short *) (memory + PC + 1))));
    }
    else if(IS_BRANCH_INSTRUCTION(currentInstruction))
    {
        struct branchInstruction branch = *((struct branchInstruction *) &currentInstruction);

        unsigned int flag;

        switch(branch.x)
        {
        case 0:
            flag = N;
            break;
        case 1:
            flag = V;
            break;
        case 2:
            flag = C;
            break;
        default:
            flag = Z;
            break;
        }

        if(flag == branch.y)
        {
            // PC would be address of branch + 2 by the time the offset is added
            PC += (signed char) memory[PC+1] + (unsigned short) 2;
        }
        else
        {
            PC += 2;
        }

    }
    else if(IS_IMPL(currentInstruction))
    {
        if(currentInstruction.c == 0)
        {
            switch(currentInstruction.b)
            {
            case 0:
                switch(currentInstruction.a)
                {
                case 0:     // BRK
                    if(!I)  // ignored if interrupt disable is set
                    {
                        B = 1;
                        PC++;
                        push(PCHI);
                        push(PCLO);
                        push(P);

                        PC = *((unsigned short *) (memory + 0xFFFE));
                    }
                    break;
                case 2:     // RTI
                    P = pull();
                case 3:     // RTS
                    PCLO = pull();
                    PCHI = pull();
                    //printf("returning to %02X\n", memory[PC]);
                    break;
                default:
                    return 0;
                }
                break;
            case 2:
                switch(currentInstruction.a)
                {
                case 0:     // PHP
                    push(P);
                    break;
                case 1:     // PLP
                    P = pull();
                    break;
                case 2:     // PHA
                    push(A);
                    break;
                case 3:     // PLA
                    A = pull();
                    break;
                case 4:     // DEY
                    Y--;
                    N = (signed char) Y < 0 ? 1 : 0;
                    Z = Y == 0 ? 1 : 0;
                    break;
                case 5:     // TAY
                    Y = A;
                    N = (signed char) Y < 0 ? 1 : 0;
                    Z = Y == 0 ? 1 : 0;
                    break;
                case 6:     // INY
                    Y++;
                    N = (signed char) Y < 0 ? 1 : 0;
                    Z = Y == 0 ? 1 : 0;
                    break;
                default:    // INX
                    X++;
                    N = (signed char) X < 0 ? 1 : 0;
                    Z = X == 0 ? 1 : 0;
                    break;
                }
                break;
            case 6:
                switch(currentInstruction.a)
                {
                    case 0:     // CLC
                        C = 0;
                        break;
                    case 1:     // SEC
                        C = 1;
                        break;
                    case 2:     // CLI
                        I = 0;
                        break;
                    case 3:     // SEI
                        I = 1;
                        break;
                    case 4:     // TYA
                        A = Y;
                    N = (signed char) A < 0 ? 1 : 0;
                    Z = A == 0 ? 1 : 0;
                        break;
                    case 5:     // CLV
                        V = 0;
                        break;
                    case 6:     // CLD
                        D = 0;
                        break;
                    default:    // SED
                        D = 1;
                        break;
                }
                break;
            default:
                return 0;
            }
        }
        else
        {
            switch(currentInstruction.b)
            {
            case 2:
                switch(currentInstruction.a)
                {
                case 4:      // TXA
                    A = X;
                    N = (signed char) A < 0 ? 1 : 0;
                    Z = A == 0 ? 1 : 0;
                    break;
                case 5:      // TAX
                    X = A;
                    N = (signed char) X < 0 ? 1 : 0;
                    Z = X == 0 ? 1 : 0;
                    break;
                case 6:      // DEX
                    X--;
                    N = (signed char) X < 0 ? 1 : 0;
                    Z = X == 0 ? 1 : 0;
                    break;
                case 7:      // NOP
                    // NOP
                    break;
                default:
                    return 0;
                }
                break;
            case 6:
                            // TXS
                if(currentInstruction.a == 4)
                {
                    S = X;
                }
                else        // TSX
                {
                    X = S;
                    N = (signed char) X < 0 ? 1 : 0;
                    Z = X == 0 ? 1 : 0;
                }
                break;
            default:
                return 0;
            }
        }
        PC++;
    }
    else
    {
        // size in bytes of the instruction (1-3) for incrementing PC
        unsigned char size = 0;

        switch(currentInstruction.c)
        {
        case 1:
            switch(currentInstruction.b)
            {
            case 0:
                addressMode = XIND;
                size = 2;
                break;
            case 1:
                addressMode = ZPG;
                size = 2;
                break;
            case 2:
                addressMode = IMME;
                size = 2;
                break;
            case 3:
                addressMode = ABS;
                size = 3;
                break;
            case 4:
                addressMode = INDY;
                size = 2;
                break;
            case 5:
                addressMode = ZPGX;
                size = 2;
                break;
            case 6:
                addressMode = ABSY;
                size = 3;
                break;
            case 7:
                addressMode = ABSX;
                size = 3;
                break;
            default:
                size = 1;
                break;
            }
            break;
        default:
            switch(currentInstruction.b)
            {
            case 0:     // some instructions in this group are implied or JSR abs, which are handled above
                addressMode = IMME;
                size = 2;
                break;
            case 1:
                addressMode = ZPG;
                size = 2;
                break;
            case 2:     // most in this group are implied, which are handled above
                addressMode = ACC;
                size = 1;
                break;
            case 3:
                addressMode = ABS;
                size = 3;
                break;
            // case 4 is only branch, handled above
            case 5:
                if(currentInstruction.c == 2 && (currentInstruction.a == 4 || currentInstruction.a == 5))
                {
                    addressMode = ZPGY;
                }
                else
                {
                    addressMode = ZPGX;
                }
                size = 2;
                break;
            case 6:
                addressMode = IMPL;
                size = 1;
                break;
            case 7:
                if(currentInstruction.c == 2 && currentInstruction.a == 5)
                {
                    addressMode = ABSY;
                }
                else
                {
                    addressMode = ABSX;
                }
                size = 3;
                break;
            default:
                break;
            }
        }

        // try store instructions first
        if(currentInstruction.a == 4)
        {
            // all b-values are STA for c = 1
            // odd b-values are STY and STX for c = 0 and 2,
            if(currentInstruction.c == 1)
            {
                store(A);
                PC += size;
                return 1;
            }
            if(currentInstruction.b % 2 == 1)
            {
                store(currentInstruction.c == 0 ? Y : X);
                PC += size;
                return 1;
            }
        }

        unsigned char data = load();

        // to keep resultant value while updating flags
        unsigned char hold;

        switch(currentInstruction.c)
        {
        case 1:
            switch(currentInstruction.a)
            {
            case 0:     // ORA
                A |= data;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            case 1:     // AND
                A &= data;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            case 2:     // EOR
                A ^= data;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            case 3:     // ADC
                if(D)
                {
                    data = (unsigned char) (((data >> 4) * 10) + (data & 0xF));
                    A = (unsigned char) (((A >> 4) * 10) + (A & 0xF));
                }

                hold = A;
                A += data + C;

                if(D)
                {
                    A = (unsigned char) (((A / 10) * 16) + (A % 10));
                }

                // TODO figure out if this sets the V flag properly
                V = ((signed char) hold >= 0 && (signed char) data >= 0 && (signed char) A < 0)
                        || ((signed char) hold < 0 && (signed char) data < 0 && (signed char) A >= 0) ? 1 : 0;

                C = (int) hold + data + C > (D ? 0x99 : 0xFF) ? 1 : 0;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            case 5:     // LDA
                A = data;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            case 6:     // CMP
                C =  A >= data ? 1 : 0;
                data = A - data;
                N = (signed char) data < 0 ? 1 : 0;
                Z = data == 0 ? 1 : 0;
                break;
            case 7:     // SBC
                data = ~data;       // flip the argument and ADC

                if(D)
                {
                    data = (unsigned char) (((data >> 4) * 10) + (data & 0xF));
                    A = (unsigned char) (((A >> 4) * 10) + (A & 0xF));
                }

                hold = A;
                A += data + C;

                if(D)
                {
                    A = (unsigned char) (((A / 10) * 16) + (A % 10));
                }

                V = ((signed char) hold >= 0 && (signed char) data >= 0 && (signed char) A < 0)
                    || ((signed char) hold < 0 && (signed char) data < 0 && (signed char) A >= 0) ? 1 : 0;

                C = (int) hold + data + C > (D ? 0x99 : 0xFF) ? 1 : 0;
                N = (signed char) A < 0 ? 1 : 0;
                Z = A == 0 ? 1 : 0;
                break;
            default:
                return 0;
            }
            break;
        case 2:
            switch(currentInstruction.a)
            {
            case 0:     // ASL
                if(addressMode == ACC)
                {
                    // sign bit goes into carry
                    C = (signed char) A < 0 ? 1 : 0;
                    A <<= 1;

                    N = (signed char) A < 0 ? 1 : 0;
                    Z = A == 0 ? 1 : 0;
                }
                else
                {
                    C = (signed char) data < 0 ? 1 : 0;
                    store(data << 1);

                    N = (signed char) data < 0 ? 1 : 0;
                    Z = data == 0 ? 1 : 0;
                }
                break;
            case 1:     // ROL
                if(addressMode == ACC)
                {
                    C = (signed char) A < 0 ? 1 : 0;
                    A = (unsigned char) ((A << 1) + C);

                    N = (signed char) A < 0 ? 1 : 0;
                    Z = A == 0 ? 1 : 0;
                }
                else
                {
                    C = (signed char) data < 0 ? 1 : 0;
                    data = (unsigned char) ((data << 1) + C);
                    store(data);

                    N = (signed char) data < 0 ? 1 : 0;
                    Z = data == 0 ? 1 : 0;
                }
                break;
            case 2:     // LSR
                if(addressMode == ACC)
                {
                    // bit 0 goes into carry
                    C = A & 1 ? 1 : 0;
                    A >>= 1;

                    Z = A == 0 ? 1 : 0;
                }
                else
                {
                    C = data & 1 ? 1 : 0;
                    store(data >> 1);

                    Z = data == 0 ? 1 : 0;
                }
                break;
            case 3:     // ROR
                if(addressMode == ACC)
                {
                    C = A & 1 ? 1 : 0;
                    A = (unsigned char) ((A >> 1) | (C ? 0x80 : 0));

                    N = (signed char) A < 0 ? 1 : 0;
                    Z = A == 0 ? 1 : 0;
                }
                else
                {
                    C = data & 1 ? 1 : 0;
                    data = (unsigned char) ((data >> 1) | (C ? 0x80 : 0));
                    store(data);

                    N = (signed char) data < 0 ? 1 : 0;
                    Z = data == 0 ? 1 : 0;
                }
                break;
            case 5:     // LDX
                X = data;
                N = (signed char) X < 0 ? 1 : 0;
                Z = X == 0 ? 1 : 0;
                break;
            case 6:     // DEC
                data--;
                store(data);

                N = (signed char) data < 0 ? 1 : 0;
                Z = data == 0 ? 1 : 0;
                break;
            case 7:     // INC
                data++;
                store(data);

                N = (signed char) data < 0 ? 1 : 0;
                Z = data == 0 ? 1 : 0;
                break;
            default:
                return 0;
            }
            break;
        case 0:
            switch(currentInstruction.a)
            {
            case 1:     // BIT
                // bits 7 and 6 of the operand are transferred to bits 7 and 6 of the status register (N and V)
                P = (unsigned char) ((data & 0b11000000) | (P & 0b00111111));
                Z = (data & A) == 0 ? 1 : 0;
                break;
            case 5:     // LDY
                Y = data;
                N = (signed char) X < 0 ? 1 : 0;
                Z = X == 0 ? 1 : 0;
                break;
            case 6:     // CPY
                C = Y >= data ? 1 : 0;
                data = Y - data;
                N = (signed char) data < 0 ? 1 : 0;
                Z = data == 0 ? 1 : 0;
                break;
            case 7:     // CPX
                C =  X >= data ? 1 : 0;
                data = X - data;
                N = (signed char) data < 0 ? 1 : 0;
                Z = data == 0 ? 1 : 0;
                break;
            default:
                return 0;
            }
            break;
        default:
            return 0;
        }
        PC += size;
    }

    return 1;
}

/**
 * continuously runs instructions
 */
static void *run(void *param)
{
    (void) param;
    pthread_mutex_lock(&runningMutex);
    running = 1;
    pthread_mutex_unlock(&runningMutex);
    //unsigned int instructionsRun = 0;

    while(executeInstruction())
    {
        pthread_mutex_lock(&runningMutex);
        if(!running)
        {
            pthread_mutex_unlock(&runningMutex);
            break;
        }
        pthread_mutex_unlock(&runningMutex);

        //instructionsRun++;
    }

    //printf("Done after %d instructions\n\n", instructionsRun);
    //printf("PC: %04X\t%02X %02X %02X\n\nA\tX\tY\tS\tN V B D I Z C\n%02X\t%02X\t%02X\t%02X\t%u %u %u %u %u %u %u\n\n", PC, memory[PC], memory[PC+1], memory[PC+2], A, X, Y, S, N, V, B, D, I, Z, C);

    unmountCards();
    closeTape();

    pthread_mutex_lock(&runningMutex);
    cpuThread = 0;
    running = 0; // in case !executeInstruction() was the reason the loop exited
    pthread_mutex_unlock(&runningMutex);
    //printf("done running\n");

    return NULL;
}

/**
 * performs the 6502 reset routine
 */
void reset()
{
    struct timespec ts;
    ts.tv_sec = 0.01;
    ts.tv_nsec = 10000000;
    nanosleep(&ts, NULL); // give previous run thread and peripheral cards time to end in case F1 was pressed

    if(!mountCards())
    {
        unmountCards();
        return;
    }

    PC = *((unsigned short *) (memory + 0xFFFC));
    S = 0xFF;
    I = 1;

    (void) pthread_create(&cpuThread, NULL, run, NULL);
}