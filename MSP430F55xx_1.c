#include <msp430.h>
#include <stdint.h>
#include <stdio.h>
#include "board_define.h"

// --- 함수 선언 ---
void SystemClock_Init(void);
void SetVCoreUp(uint8_t level);
void UART_Init_115200(void);
void UART_PrintString(char *str);
void UART_PrintHex16(uint16_t n);
void SPI_Config_Mode(int mode);
uint16_t DRV_ReadReg(uint8_t addr);

// =============================================================
// 메인 함수
// =============================================================
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;

    // 1. 시스템 초기화
    SystemClock_Init();   // 24MHz
    UART_Init_115200();   
    
    // SPI 핀 (P3.0, P3.1, P3.2)
    P3SEL |= BIT0 | BIT1 | BIT2; 
    P3DIR |= BIT0 | BIT2; // MOSI, CLK -> Out
    P3DIR &= ~BIT1;       // MISO -> In
    
    // [수정 완료] CS 핀 설정: P2.6 -> P2.0
    P2DIR |= BIT0; P2OUT |= BIT0; // CS High (Inactive)

    __enable_interrupt(); 

    UART_PrintString("\r\n\r\n=== SPI Doctor: Pin Corrected (P2.0) ===\r\n");
    UART_PrintString("Target: DRV8305 Register 0x07\r\n");
    
    // 2. DRV8305 깨우기
    UART_PrintString("1. Waking up DRV8305... ");
    DRV_WAKE_PORT |= DRV_WAKE_PIN; 
    __delay_cycles(2400000); 
    DRV_EN_PORT |= DRV_EN_PIN;     
    __delay_cycles(240000); 
    UART_PrintString("Done.\r\n");

    // 3. 모드 스캔 루프
    int mode = 0;
    while (1) {
        UART_PrintString("\r\nTesting Mode ");
        // 숫자 출력 (간단히)
        while(!(UCA1IFG & UCTXIFG)); UCA1TXBUF = mode + '0';
        UART_PrintString("... ");

        // SPI 설정 변경
        SPI_Config_Mode(mode);

        // 테스트 읽기
        uint16_t val = DRV_ReadReg(0x07); 

        // 결과 출력
        UART_PrintString("Read: ");
        UART_PrintHex16(val);

        // 성공 판독 (0이 아니면 성공!)
        if (val != 0x0000 && val != 0xFFFF) {
            UART_PrintString("  <-- [JACKPOT] Communication Success!");
        }

        mode++;
        if (mode > 3) mode = 0;

        __delay_cycles(24000000); // 1초 대기
    }
}

// =============================================================
// SPI 설정 (모드 스캔용)
// =============================================================
void SPI_Config_Mode(int mode) {
    UCB0CTL1 |= UCSWRST; 
    uint8_t ctl0 = UCMST + UCSYNC + UCMSB;

    switch (mode) {
        case 0: ctl0 |= UCCKPH; break;          // CKPL=0, CKPH=1
        case 1: break;                          // CKPL=0, CKPH=0
        case 2: ctl0 |= UCCKPL + UCCKPH; break; // CKPL=1, CKPH=1
        case 3: ctl0 |= UCCKPL; break;          // CKPL=1, CKPH=0
    }
    
    UCB0CTL0 = ctl0;
    UCB0CTL1 |= UCSSEL_2; 
    UCB0BR0 = 48; // 500kHz (안전)
    UCB0BR1 = 0;
    UCB0CTL1 &= ~UCSWRST; 
    __delay_cycles(2400); 
}

// =============================================================
// SPI Read (CS핀 P2.0으로 수정됨)
// =============================================================
uint16_t DRV_ReadReg(uint8_t addr) {
    uint16_t cmd = (1 << 15) | ((addr & 0x0F) << 11);
    uint16_t res = 0;
    
    // [수정] CS Low (P2.0)
    P2OUT &= ~BIT0; 
    __delay_cycles(100); 

    while(!(UCB0IFG & UCTXIFG)); UCB0TXBUF = (cmd >> 8) & 0xFF;
    while(!(UCB0IFG & UCRXIFG)); res = UCB0RXBUF << 8;

    while(!(UCB0IFG & UCTXIFG)); UCB0TXBUF = 0x00;
    while(!(UCB0IFG & UCRXIFG)); res |= UCB0RXBUF;

    // [수정] CS High (P2.0)
    __delay_cycles(50); 
    P2OUT |= BIT0; 
    
    return res & 0x07FF; 
}

// =============================================================
// 기타 필수 함수 (기존 유지)
// =============================================================
void SetVCoreUp(uint8_t level) { 
    if ((PMMCTL0 & PMMCOREV_3) >= level) return; 
    PMMCTL0_H = PMMPW_H;
    while ((PMMCTL0 & PMMCOREV_3) < level) {
        uint8_t next = (PMMCTL0 & PMMCOREV_3) + 1;
        SVSMHCTL = SVSHE + SVSHRVL0 * next + SVMHE + SVSMHRRL0 * next;
        while ((PMMIFG & SVSMHDLYIFG) == 0); PMMIFG &= ~(SVMHVLRIFG + SVSMHDLYIFG);
        PMMCTL0_L = PMMCOREV0 * next;
        if ((PMMIFG & SVMLIFG)) while ((PMMIFG & SVMLVLRIFG) == 0);
        SVSMLCTL = SVSLE + SVSLRVL0 * next + SVMLE + SVSMLRRL0 * next;
        while ((PMMIFG & SVSMLDLYIFG) == 0); PMMIFG &= ~(SVMLVLRIFG + SVSMLDLYIFG);
    }
    PMMCTL0_H = 0x00;
}
void SystemClock_Init(void) {
    SetVCoreUp(1); SetVCoreUp(2); SetVCoreUp(3);
    UCSCTL3 = SELREF_2; UCSCTL4 |= SELA_2;
    __bis_SR_register(SCG0);
    UCSCTL0 = 0x0000; UCSCTL1 = DCORSEL_7; UCSCTL2 = FLLD_0 + 732; 
    __bic_SR_register(SCG0);
    __delay_cycles(250000);
    do { UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG); SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
}
void UART_Init_115200(void) {
    P4SEL |= BIT4|BIT5; UCA1CTL1 |= UCSWRST; UCA1CTL1 |= UCSSEL_2; 
    UCA1BR0 = 208; UCA1BR1 = 0; UCA1MCTL = UCBRS_3; UCA1CTL1 &= ~UCSWRST;
}
void UART_PrintString(char *str) { while(*str) { while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = *str++; } }

void UART_PrintHex16(uint16_t n) {
    char hex[] = "0123456789ABCDEF";
    char buf[4];
    int i; 
    UART_PrintString("0x");
    for(i=3; i>=0; i--) { buf[i] = hex[n & 0xF]; n >>= 4; }
    for(i=0; i<4; i++) { while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = buf[i]; }
}
