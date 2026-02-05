#include <msp430.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "board_define.h"

// --- 설정 상수 ---
#define PWM_PERIOD  1000        // 1kHz Frequency
#define PWM_DUTY    100         // 10% Duty (Safety First!)

// --- 함수 선언 ---
void BSP_Init(void);
void SPI_Init(void);
void UART_Init(void);
void UART_Printf(char *format, ...);
void UART_PrintHex16(uint16_t value);

uint16_t DRV_ReadReg(uint8_t addr);
void DRV_WriteReg(uint8_t addr, uint16_t data);
void DRV_Init_Registers(void);

void Timer_Init(void);
void Commutate_Step(uint8_t step);
void Check_Faults(void);

// --- 메인 함수 ---
int main(void) {
    BSP_Init();     
    SPI_Init();     
    UART_Init();    
    Timer_Init();   // 타이머 초기화 (PWM 준비)

    __delay_cycles(1000000); 
    UART_Printf("\r\n=== Step 5: First Spin (Open Loop) ===\r\n");

    // 1. DRV8305 Wake Up
    DRV_WAKE_PORT |= DRV_WAKE_PIN;
    __delay_cycles(100000);
    
    // 2. Register Config (Safety)
    DRV_Init_Registers();

    // 3. EN_GATE Enable (이제 모터에 전기가 들어갑니다!)
    DRV_EN_PORT |= DRV_EN_PIN;
    // DRV_EN_PORT &= ~DRV_EN_PIN;
    UART_Printf("WARNING: Motor Driver ENABLED! (Duty 10%%)\r\n");
    __delay_cycles(10000);

    // 4. 6-Step Commutation Loop
    uint8_t step = 0;
    while (1) {
        Check_Faults();
        Commutate_Step(step); // 스텝 변경
        
        UART_Printf("Step: %d\r\n", step);
        
        step++;
        if (step > 5) step = 0;

        P1OUT ^= BIT0; // LED Toggle
        
        // 속도 조절: 0.5초마다 한 칸씩 이동 (눈으로 확인 가능)
        __delay_cycles(500000); 
    }
}

// --- 6-Step Commutation 함수 ---
// 각 스텝마다 전류가 흐르는 길을 열어줍니다.
// High Side는 PWM(10%), Low Side는 ON(100%) 상태로 구동합니다.
void Commutate_Step(uint8_t step) {
    // 일단 모든 출력 끄기 (Reset) - 안전을 위해
    // 출력 모드 0 (OUT bit control), 출력 0
    TA2CCTL1 = OUTMOD_0; TA2CCTL1 &= ~OUT; // A_L
    TA2CCTL2 = OUTMOD_0; TA2CCTL2 &= ~OUT; // A_H
    TA0CCTL3 = OUTMOD_0; TA0CCTL3 &= ~OUT; // B_L
    TA0CCTL4 = OUTMOD_0; TA0CCTL4 &= ~OUT; // B_H
    TB0CCTL5 = OUTMOD_0; TB0CCTL5 &= ~OUT; // C_L
    TB0CCTL2 = OUTMOD_0; TB0CCTL2 &= ~OUT; // C_H

    switch (step) {
        case 0: // Step 1: A+ B- (A High PWM, B Low ON)
            TA2CCR2 = PWM_DUTY; TA2CCTL2 = OUTMOD_7; // A_H PWM
            TA0CCTL3 = OUTMOD_0; TA0CCTL3 |= OUT;    // B_L ON
            break;
            
        case 1: // Step 2: A+ C-
            TA2CCR2 = PWM_DUTY; TA2CCTL2 = OUTMOD_7; // A_H PWM
            TB0CCTL5 = OUTMOD_0; TB0CCTL5 |= OUT;    // C_L ON
            break;
            
        case 2: // Step 3: B+ C-
            TA0CCR4 = PWM_DUTY; TA0CCTL4 = OUTMOD_7; // B_H PWM
            TB0CCTL5 = OUTMOD_0; TB0CCTL5 |= OUT;    // C_L ON
            break;
            
        case 3: // Step 4: B+ A-
            TA0CCR4 = PWM_DUTY; TA0CCTL4 = OUTMOD_7; // B_H PWM
            TA2CCTL1 = OUTMOD_0; TA2CCTL1 |= OUT;    // A_L ON
            break;
            
        case 4: // Step 5: C+ A-
            TB0CCR2 = PWM_DUTY; TB0CCTL2 = OUTMOD_7; // C_H PWM
            TA2CCTL1 = OUTMOD_0; TA2CCTL1 |= OUT;    // A_L ON
            break;
            
        case 5: // Step 6: C+ B-
            TB0CCR2 = PWM_DUTY; TB0CCTL2 = OUTMOD_7; // C_H PWM
            TA0CCTL3 = OUTMOD_0; TA0CCTL3 |= OUT;    // B_L ON
            break;
    }
}

// --- 타이머 초기화 (기본 설정만) ---
void Timer_Init(void) {
    // 핀 설정 (Peripheral 모드)
    P2DIR |= (BIT4 | BIT5); P2SEL |= (BIT4 | BIT5); // A
    P1DIR |= (BIT4 | BIT5); P1SEL |= (BIT4 | BIT5); // B
    P7DIR |= BIT4; P7SEL |= BIT4; // C_H
    P3DIR |= BIT5; P3SEL |= BIT5; // C_L

    // Timer A2 (Phase A)
    TA2CCR0 = PWM_PERIOD - 1;
    TA2CTL = TASSEL_2 + MC_1 + TACLR; // SMCLK, Up Mode

    // Timer A0 (Phase B)
    TA0CCR0 = PWM_PERIOD - 1;
    TA0CTL = TASSEL_2 + MC_1 + TACLR;

    // Timer B0 (Phase C)
    TB0CCR0 = PWM_PERIOD - 1;
    TB0CTL = TBSSEL_2 + MC_1 + TBCLR;
}

//============================================================================
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

// --- 실시간 에러 감지 및 리포팅 함수 ---
void Check_Faults(void) {
    // nFAULT 핀이 Low(0)로 떨어졌는지 확인 (Active Low)
    if ((DRV_FAULT_PORT & DRV_FAULT_PIN) == 0) {
        
        // 1. 즉시 모터 정지 (안전 제일!)
        DRV_EN_PORT &= ~DRV_EN_PIN; // Enable 끄기
        TA2CCTL1 = 0; TA2CCTL2 = 0; // PWM 타이머 모두 정지
        TA0CCTL3 = 0; TA0CCTL4 = 0;
        TB0CCTL2 = 0; TB0CCTL5 = 0;
        
        UART_Printf("\r\n[EMERGENCY] Fault Detected! Motor Stopped.\r\n");

        // 2. 에러 레지스터 읽기 (0x01: Warning, 0x02: VDS, 0x03: IC)
        uint16_t stat_reg01 = DRV_ReadReg(0x01);
        uint16_t vds_reg02  = DRV_ReadReg(0x02);
        uint16_t ic_reg03   = DRV_ReadReg(0x03);

        // 3. Status Reg 0x01 분석
        UART_Printf(">> 0x01 (Status): "); UART_PrintHex16(stat_reg01); UART_Printf("\r\n");
        if (stat_reg01 & 0x0400) UART_Printf("   - FAULT Pin Asserted\r\n");
        if (stat_reg01 & 0x0100) UART_Printf("   - VDS (Overcurrent) Detected\r\n");
        if (stat_reg01 & 0x0080) UART_Printf("   - UVLO (Undervoltage)\r\n");
        if (stat_reg01 & 0x0040) UART_Printf("   - Overtemperature\r\n");

        // 4. VDS Reg 0x02 분석 (어느 MOSFET이 터졌나?)
        if (vds_reg02 != 0) {
            UART_Printf(">> 0x02 (VDS Faults): "); UART_PrintHex16(vds_reg02); UART_Printf("\r\n");
            // High Side Checks
            if (vds_reg02 & 0x0001) UART_Printf("   - High Side A (Overcurrent)\r\n");
            if (vds_reg02 & 0x0002) UART_Printf("   - Low Side A (Overcurrent)\r\n");
            if (vds_reg02 & 0x0004) UART_Printf("   - High Side B (Overcurrent)\r\n");
            // Low Side Checks
            if (vds_reg02 & 0x0008) UART_Printf("   - Low Side B (Overcurrent)\r\n");
            if (vds_reg02 & 0x0010) UART_Printf("   - High Side C (Overcurrent)\r\n");
            if (vds_reg02 & 0x0020) UART_Printf("   - Low Side C (Overcurrent)\r\n");
        }

        // 5. IC Faults 0x03 분석
        if (ic_reg03 != 0) {
            UART_Printf(">> 0x03 (IC Faults): "); UART_PrintHex16(ic_reg03); UART_Printf("\r\n");
            if (ic_reg03 & 0x0400) UART_Printf("   - PVDD Undervoltage\r\n");
            if (ic_reg03 & 0x0080) UART_Printf("   - VCP Charge Pump Fail\r\n");
        }

        UART_Printf("Action: Check wiring & Reset Board.\r\n");
        
        // 6. 무한 루프 (재부팅 전까지 동작 중지)
        while(1) {
            P1OUT ^= BIT0; // LED 빠르게 깜빡임 (에러 표시)
            __delay_cycles(200000);
        }
    }
}
