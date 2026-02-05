#include <msp430.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "board_define.h"

// --- 함수 선언 ---
void BSP_Init(void);
void SPI_Init(void);
void UART_Init(void);
void UART_Printf(char *format, ...);
void UART_PrintHex16(uint16_t value);

uint16_t DRV_ReadReg(uint8_t addr);
void DRV_WriteReg(uint8_t addr, uint16_t data);
void DRV_Init_Registers(void);
void PWM_Init(void);

// --- 메인 함수 ---
int main(void) {
    BSP_Init();     // 하드웨어 초기화
    SPI_Init();     // SPI 초기화 (CPHA 수정됨)
    UART_Init();    // UART 초기화

    __delay_cycles(1000000); // 전원 안정화 대기
    UART_Printf("\r\n=== System Booting... ===\r\n");

    // 1. DRV8305 깨우기 (WAKE)
    DRV_WAKE_PORT |= DRV_WAKE_PIN;
    __delay_cycles(100000); 
    
    // 2. EN_GATE 활성화 (설정을 위해 Enable 필요)
    DRV_EN_PORT |= DRV_EN_PIN;
    __delay_cycles(10000); 

    UART_Printf("[DRV8305] Waking up & Enable...\r\n");

    // 3. [핵심] 레지스터 설정 및 검증
    DRV_Init_Registers();

    UART_Printf("[System] Ready. Monitoring Status...\r\n");

    // 4. 상태 모니터링
    while (1) {
        uint16_t status = DRV_ReadReg(0x01); // Status
        uint16_t vds = DRV_ReadReg(0x02);    // VDS Faults
        
        UART_Printf("Stat(0x01): "); UART_PrintHex16(status);
        UART_Printf(" | VDS(0x02): "); UART_PrintHex16(vds);
        UART_Printf("\r\n");
        
        P1OUT ^= BIT0; // LED 깜빡임
        __delay_cycles(8000000); // 1초 대기
    }
}

// --- 레지스터 초기화 함수 ---
void DRV_Init_Registers(void) {
    UART_Printf(">>> Configuring Registers...\r\n");

    // 1. Gate Drive Strength: 40mA/50mA (Soft)
    DRV_WriteReg(0x05, 0x0333); 
    DRV_WriteReg(0x06, 0x0333);

    // 2. PWM Mode: 6-PWM, Deadtime: 1760ns (Safety)
    // Value: 0x0056
    DRV_WriteReg(0x07, 0x0056);

    // 3. VDS Protection: 0.403V, Latched Shutdown
    // Value: 0x0080
    DRV_WriteReg(0x0C, 0x0080);

    // 4. 검증 (0x07번지 확인)
    uint16_t check07 = DRV_ReadReg(0x07);
    UART_Printf("Check Reg 0x07: "); UART_PrintHex16(check07);
    
    if ((check07 & 0x00FF) == 0x0056) {
        UART_Printf(" -> OK (Config Success)\r\n");
    } else {
        UART_Printf(" -> FAIL! Check SPI Write.\r\n");
    }
}

// ... (나머지 BSP_Init, SPI_Init, UART 함수들은 이전과 동일) ...
// (특히 SPI_Init에서 UCCKPH 비트 제거된 것 유지해주세요!)

// ============================================================================
// [함수 구현부 (Definitions)]
// ============================================================================

/**
 * 하드웨어 핀 초기화 (board_define.h 설정 기반)
 */
void BSP_Init(void) {
    WDTCTL = WDTPW | WDTHOLD;   // Watchdog Stop

    // 1. DRV8305 제어 핀 설정 (User Map 적용)
    // SCS (P2.0) -> Output, High
    DRV_CS_DIR |= DRV_CS_PIN;
    DRV_CS_PORT |= DRV_CS_PIN;

    // ENGATE (P2.6) -> Output, Low
    DRV_EN_DIR |= DRV_EN_PIN;
    DRV_EN_PORT &= ~DRV_EN_PIN;

    // WAKE (P2.3) -> Output, Low
    DRV_WAKE_DIR |= DRV_WAKE_PIN;
    DRV_WAKE_PORT &= ~DRV_WAKE_PIN;

    // FAULT (P3.4) -> Input (Pull-up 권장)
    DRV_FAULT_DIR &= ~DRV_FAULT_PIN;
    P3REN |= DRV_FAULT_PIN; 
    P3OUT |= DRV_FAULT_PIN;

    // 2. SPI 핀 설정 (P3.0, P3.2 Output / P3.1 Input)
    SPI_DIR_PORT |= (SPI_SIMO_PIN | SPI_CLK_PIN);
    SPI_DIR_PORT &= ~SPI_SOMI_PIN;
    SPI_SEL_PORT |= (SPI_SIMO_PIN | SPI_SOMI_PIN | SPI_CLK_PIN);

    // 3. LED (P1.0) 설정
    P1DIR |= BIT0; P1OUT &= ~BIT0;
}

/**
 * SPI 모듈 (USCI_B0) 초기화
 */
void SPI_Init(void) {
    // 1. SPI 모듈 리셋 (설정 변경 전 필수)
    UCB0CTL1 |= UCSWRST;

    // 2. SPI 모드 설정 (DRV8305 Page 34 반영)
    // - UCMSB: MSB First [cite: 3658]
    // - UCMST: Master Mode
    // - UCSYNC: Synchronous Mode
    // - UCCKPL = 0 (기본값): Clock Idle Low [cite: 3653, 3654]
    // - UCCKPH = 0 (비트 제거!): Data changed on 1st edge, captured on 2nd edge [cite: 3653, 3657]
    UCB0CTL0 = UCMSB + UCMST + UCSYNC; // (UCCKPH 비트 제거됨)

    // 3. 클럭 소스 선택 (SMCLK)
    UCB0CTL1 |= UCSSEL_2;

    // 4. 통신 속도 설정
    // DRV8305 최소 클럭 주기는 100ns(10MHz)이므로[cite: 2656],
    // 1MHz 설정(BR0=1)은 아주 안전한 속도입니다.
    UCB0BR0 = 1;
    UCB0BR1 = 0;

    // 5. 모듈 활성화
    UCB0CTL1 &= ~UCSWRST;
}

/**
 * UART 모듈 (USCI_A1) 초기화 (115200bps @ 8MHz)
 */
void UART_Init(void) {
    P4SEL |= BIT4 | BIT5;       // P4.4(TX), P4.5(RX)
    UCA1CTL1 |= UCSWRST;
    UCA1CTL1 |= UCSSEL_2;       // SMCLK
    
    // 1MHz에서 115200bps 설정 (동작했던 코드 값 적용)
    UCA1BR0 = 9;                // 1,048,576 / 115,200 = 9.1
    UCA1BR1 = 0;
    // Modulation 설정 (동작했던 코드 값 적용)
    UCA1MCTL = UCBRS_1 + UCBRF_0;
    
    UCA1CTL1 &= ~UCSWRST;
}

/**
 * UART printf 구현
 */
void UART_Printf(char *format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    char *str = buffer;
    while(*str) {
        while (!(UCA1IFG & UCTXIFG));
        UCA1TXBUF = *str++;
    }
}

/**
 * DRV8305 레지스터 읽기 함수
 */
uint16_t DRV_ReadReg(uint8_t addr) {
    uint16_t txData;
    uint8_t highByte, lowByte;

    // Read Command: MSB=1 (Bit 15)
    // Address: Bit 11-14
    txData = (1 << 15) | ((addr & 0x0F) << 11);

    // CS Low
    DRV_CS_PORT &= ~DRV_CS_PIN;
    __delay_cycles(20);

    // High Byte 전송
    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = (txData >> 8);
    while (!(UCB0IFG & UCRXIFG));
    highByte = UCB0RXBUF;

    // Low Byte 전송 (Dummy)
    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = (txData & 0xFF);
    while (!(UCB0IFG & UCRXIFG));
    lowByte = UCB0RXBUF;

    // CS High
    __delay_cycles(20);
    DRV_CS_PORT |= DRV_CS_PIN;

    // 데이터 조합 (상위 5비트는 무시)
    return ((highByte << 8) | lowByte) & 0x07FF;
}

/**
 * DRV8305 레지스터 쓰기 함수
 */
void DRV_WriteReg(uint8_t addr, uint16_t data) {
    uint16_t txData;
    
    // Write Command: MSB=0
    txData = ((addr & 0x0F) << 11) | (data & 0x07FF);

    DRV_CS_PORT &= ~DRV_CS_PIN;
    __delay_cycles(20);

    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = (txData >> 8);
    while (!(UCB0IFG & UCRXIFG));
    (void)UCB0RXBUF; // Dummy Read

    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = (txData & 0xFF);
    while (!(UCB0IFG & UCRXIFG));
    (void)UCB0RXBUF; // Dummy Read

    __delay_cycles(20);
    DRV_CS_PORT |= DRV_CS_PIN;
}

// 16비트 변수를 4자리 Hex 문자열로 변환하여 바로 출력하는 함수
void UART_PrintHex16(uint16_t value) {
    char hexTable[] = "0123456789ABCDEF";
    
    // 상위 4비트씩 잘라서 문자로 변환
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = '0';
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = 'x';
    
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = hexTable[(value >> 12) & 0xF];
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = hexTable[(value >> 8) & 0xF];
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = hexTable[(value >> 4) & 0xF];
    while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = hexTable[value & 0xF];
}

// --- PWM 초기화 함수 (20kHz 생성) ---
void PWM_Init(void) {
    // 1. MCU 클럭 설정 (PWM 해상도를 위해 8MHz 이상 권장하지만, 일단 기본 1MHz로 테스트)
    // SMCLK = 1MHz 가정 -> Period = 50 (20kHz)
    // 좀 더 부드러운 테스트를 위해 주기를 길게 잡겠습니다. (Period = 1000 -> 1kHz)
    // 오실로스코프로 보기 편하게 1kHz로 설정합니다.
    
    uint16_t pwm_period = 1000 - 1; 

    // --- Phase A (Timer_A2): P2.4(Low), P2.5(High) ---
    P2DIR |= (BIT4 | BIT5);
    P2SEL |= (BIT4 | BIT5); // P2.4, P2.5를 타이머 출력으로 연결
    
    TA2CCR0 = pwm_period;           // 주기 설정
    TA2CCTL1 = OUTMOD_7;            // Reset/Set 모드 (PWM)
    TA2CCR1 = pwm_period / 2;       // Duty 50% (P2.4)
    TA2CCTL2 = OUTMOD_7;
    TA2CCR2 = pwm_period / 2;       // Duty 50% (P2.5)
    TA2CTL = TASSEL_2 + MC_1 + TACLR; // SMCLK, Up Mode

    // --- Phase B (Timer_A0): P1.4(Low), P1.5(High) ---
    P1DIR |= (BIT4 | BIT5);
    P1SEL |= (BIT4 | BIT5);
    
    TA0CCR0 = pwm_period;
    TA0CCTL3 = OUTMOD_7;            // TA0.3 -> P1.4
    TA0CCR3 = pwm_period / 2;
    TA0CCTL4 = OUTMOD_7;            // TA0.4 -> P1.5
    TA0CCR4 = pwm_period / 2;
    TA0CTL = TASSEL_2 + MC_1 + TACLR;

    // --- Phase C (Timer_B0): P3.5(Low), P7.4(High) ---
    // 주의: P7.4는 TB0.2, P3.5는 TB0.5에 해당
    P7DIR |= BIT4; P7SEL |= BIT4;   // P7.4
    P3DIR |= BIT5; P3SEL |= BIT5;   // P3.5
    
    TB0CCR0 = pwm_period;
    TB0CCTL2 = OUTMOD_7;            // TB0.2 -> P7.4 (High)
    TB0CCR2 = pwm_period / 2;
    TB0CCTL5 = OUTMOD_7;            // TB0.5 -> P3.5 (Low)
    TB0CCR5 = pwm_period / 2;
    TB0CTL = TBSSEL_2 + MC_1 + TBCLR;
}
