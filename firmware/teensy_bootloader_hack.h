////////////////////////////////////////////////////////////////////////////////
// Teensy++ 2.0 bootloader hack
// The purpose of this is to be able to do self-programming on a Teensy++ 2.0.
// That's the only way I found to do it, hopefully PJRC will publish an API
// to do that in a properer way someday...
// Please note I didn't have a copy of the bootloader source code to do it,
// I probed it using rcall from lower NRWW.
////////////////////////////////////////////////////////////////////////////////

#ifndef TEENSY_BOOTLOADER_HACK_H
#define	TEENSY_BOOTLOADER_HACK_H

#include <avr/boot.h>

#define blHack_call_SPM "rcall .bootloader+0xe4a \n\t" // address of a SPM followed by a RET in the Teensy++ bootloader

#define blHack_page_erase(address)      \
(__extension__({                                 \
    __asm__ __volatile__                         \
    (                                            \
        "movw r30, %A3\n\t"                      \
        "sts  %1, %C3\n\t"                       \
        "sts %0, %2\n\t"                         \
        blHack_call_SPM                          \
        :                                        \
        : "i" (_SFR_MEM_ADDR(__SPM_REG)),        \
          "i" (_SFR_MEM_ADDR(RAMPZ)),            \
          "r" ((uint8_t)(__BOOT_PAGE_ERASE)),    \
          "r" ((uint32_t)(address))              \
        : "r30", "r31"                           \
    );                                           \
}))


#define blHack_page_fill(address, data) \
(__extension__({                                 \
    __asm__ __volatile__                         \
    (                                            \
        "movw  r0, %4\n\t"                       \
        "movw r30, %A3\n\t"                      \
        "sts %1, %C3\n\t"                        \
        "sts %0, %2\n\t"                         \
        blHack_call_SPM                          \
        "clr  r1\n\t"                            \
        :                                        \
        : "i" (_SFR_MEM_ADDR(__SPM_REG)),        \
          "i" (_SFR_MEM_ADDR(RAMPZ)),            \
          "r" ((uint8_t)(__BOOT_PAGE_FILL)),     \
          "r" ((uint32_t)(address)),             \
          "r" ((uint16_t)(data))                 \
        : "r0", "r30", "r31"                     \
    );                                           \
}))

#define blHack_page_write(address)      \
(__extension__({                                 \
    __asm__ __volatile__                         \
    (                                            \
        "movw r30, %A3\n\t"                      \
        "sts %1, %C3\n\t"                        \
        "sts %0, %2\n\t"                         \
        blHack_call_SPM                          \
        :                                        \
        : "i" (_SFR_MEM_ADDR(__SPM_REG)),        \
          "i" (_SFR_MEM_ADDR(RAMPZ)),            \
          "r" ((uint8_t)(__BOOT_PAGE_WRITE)),    \
          "r" ((uint32_t)(address))              \
        : "r30", "r31"                           \
    );                                           \
}))

#define blHack_rww_enable()                      \
(__extension__({                                 \
    __asm__ __volatile__                         \
    (                                            \
        "sts %0, %1\n\t"                         \
        blHack_call_SPM                          \
        :                                        \
        : "i" (_SFR_MEM_ADDR(__SPM_REG)),        \
          "r" ((uint8_t)(__BOOT_RWW_ENABLE))     \
    );                                           \
}))


#endif	/* TEENSY_BOOTLOADER_HACK_H */

