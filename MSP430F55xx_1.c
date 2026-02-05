#include <msp430.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "board_define.h"

// --- 설정 상수 ---
// 24MHz 클럭 기준
// PWM 주파수 20kHz = 24,000,000 / 20,000 = 1200 ticks
#define PWM_PERIOD      1200        
#define PWM_DUTY_INIT   180         // [수정 1] 초기 토크 상향 (10% -> 15%) 1200 * 0.15 = 180

// --- 함수 선언 ---
void SystemClock_Init(void); // [신규] 24MHz 클럭 설정
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
void SetVCoreUp(uint8_t level);

// --- 메인 함수 ---
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;   // Watchdog Stop

    SystemClock_Init(); // 1. CPU 속도를 24MHz로 높임 (필수!)
    BSP_Init();     
    SPI_Init();     
    UART_Init();    
    Timer_Init();       // PWM 20kHz 설정

    __delay_cycles(24000000); // 1초 대기 (클럭이 빨라져서 숫자도 커짐)
    UART_Printf("\r\n=== Step 6: Silent & Smooth Run ===\r\n");
    UART_Printf("System Clock: 24MHz, PWM: 20kHz\r\n");

    // 1. DRV8305 Wake Up & Config
    DRV_WAKE_PORT |= DRV_WAKE_PIN;
    __delay_cycles(240000); // 10ms
    
    DRV_Init_Registers(); // 레지스터 설정 (전류 등)

    // 2. Enable Motor Driver
    DRV_EN_PORT |= DRV_EN_PIN;
    UART_Printf("Motor Enabled. Ramping up...\r\n");
    __delay_cycles(24000); // 1ms

    // 3. 변수 초기화
    uint8_t step = 0;
    uint32_t step_delay = 50000; // 초기 딜레이 (천천히 시작)
    uint32_t min_delay = 2500;   // 최대 속도 제한
    
    // [신규] 가속 타이밍 조절 변수
    uint8_t accel_counter = 0;

    // 4. 무한 루프 (가속 구동)
    while (1) {
        Check_Faults();       
        Commutate_Step(step); 
        
        step++;
        if (step > 5) step = 0;

        // 딜레이 (현재 속도 유지)
        volatile uint32_t i;
        for(i=0; i<step_delay; i++); 

        // [수정 2] 가속 로직 개선 (Soft Start)
        // 매 스텝마다 빨라지면 모터가 체합니다.
        // "10번 걸을 때마다" 조금씩 빨라지도록 변경
        accel_counter++;
        if (accel_counter > 10) { 
            accel_counter = 0;
            
            if (step_delay > min_delay) {
                // 딜레이 감소폭을 100 -> 20으로 줄임 (아주 부드러운 가속)
                step_delay -= 20; 
            }
        }
    }
}


// [신규 추가] PMM(Power Management Module) 전압 설정 함수
void SetVCoreUp(uint8_t level) {
    // 현재 레벨보다 높을 때만 실행
    if ((PMMCTL0 & PMMCOREV_3) >= level) return; 

    // PMM 레지스터 잠금 해제
    PMMCTL0_H = PMMPW_H;

    // 목표 레벨까지 한 단계씩 상승
    while ((PMMCTL0 & PMMCOREV_3) < level) {
        uint8_t next_level = (PMMCTL0 & PMMCOREV_3) + 1;
        
        // 1. High-side 전압 설정
        SVSMHCTL = SVSHE + SVSHRVL0 * next_level + SVMHE + SVSMHRRL0 * next_level;
        while ((PMMIFG & SVSMHDLYIFG) == 0); // High-side Delay 대기
        PMMIFG &= ~(SVMHVLRIFG + SVSMHDLYIFG); // 플래그 클리어

        // 2. Core 전압 설정 (실제 승압)
        PMMCTL0_L = PMMCOREV0 * next_level;
        // 전압 레벨 도달 대기
        if ((PMMIFG & SVMLIFG)) while ((PMMIFG & SVMLVLRIFG) == 0);

        // 3. Low-side 설정
        SVSMLCTL = SVSLE + SVSLRVL0 * next_level + SVMLE + SVSMLRRL0 * next_level;
        while ((PMMIFG & SVSMLDLYIFG) == 0); // Low-side Delay 대기 (표준 매크로 사용)
        
        // 플래그 클리어 (Level Reached + Delay)
        PMMIFG &= ~(SVMLVLRIFG + SVSMLDLYIFG); 
    }

    // PMM 레지스터 잠금
    PMMCTL0_H = 0x00;
}

// [수정됨] 24MHz 시스템 클럭 초기화 (VCORE 설정 포함)
void SystemClock_Init(void) {
    // 1. [핵심] 코어 전압을 Level 3 (최대 성능)으로 승압
    // 24MHz를 쓰려면 반드시 Level 3이어야 함 (Datasheet 규격)
    SetVCoreUp(1);
    SetVCoreUp(2);
    SetVCoreUp(3); 

    // 2. FLL 초기화 (24MHz 설정)
    UCSCTL3 = SELREF_2;                       // FLL Reference = REFO (32.768kHz 내부 소스)
    UCSCTL4 |= SELA_2;                        // ACLK = REFO
    
    __bis_SR_register(SCG0);                  // FLL 루프 비활성화 (설정 시작)
    UCSCTL0 = 0x0000;                         // DCOx, MODx 최저로 리셋
    UCSCTL1 = DCORSEL_7;                      // DCO Range 설정 (High Frequency)
    // 24MHz / 32768Hz = 732.42 -> 732
    UCSCTL2 = FLLD_0 + 732;                   // Multiplier 설정 (N=732)
    __bic_SR_register(SCG0);                  // FLL 루프 활성화

    // 3. FLL 안정화 대기
    // (전압이 충분하므로 이제 빨리 안정화됩니다)
    __delay_cycles(250000); 

    // 4. 오실레이터 폴트 플래그 클리어 (Loop)
    // DCO가 목표 주파수에 도달할 때까지 에러 플래그를 계속 지워줌
    do {
        UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG);
        SFRIFG1 &= ~OFIFG;
    } while (SFRIFG1 & OFIFG);
}

// --- 타이머 초기화 (20kHz로 변경) ---
void Timer_Init(void) {
    // 핀 설정은 기존과 동일
    P2DIR |= (BIT4 | BIT5); P2SEL |= (BIT4 | BIT5);
    P1DIR |= (BIT4 | BIT5); P1SEL |= (BIT4 | BIT5);
    P7DIR |= BIT4; P7SEL |= BIT4;
    P3DIR |= BIT5; P3SEL |= BIT5;

    // Timer A2 (Phase A)
    TA2CCR0 = PWM_PERIOD - 1;       // 1200 counts (20kHz)
    TA2CTL = TASSEL_2 + MC_1 + TACLR; // SMCLK(24MHz), Up Mode

    // Timer A0 (Phase B)
    TA0CCR0 = PWM_PERIOD - 1;
    TA0CTL = TASSEL_2 + MC_1 + TACLR;

    // Timer B0 (Phase C)
    TB0CCR0 = PWM_PERIOD - 1;
    TB0CTL = TBSSEL_2 + MC_1 + TBCLR;
}

// --- UART 속도 재설정 (24MHz 기준) ---
void UART_Init(void) {
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 |= UCSWRST;
    UCA1CTL1 |= UCSSEL_2; // SMCLK (24MHz)
    // 24MHz / 115200 = 208.33
    // UCBR = 208, UCBRS = 0, UCBRF = 0 (간단 설정)
    UCA1BR0 = 208; 
    UCA1BR1 = 0;
    UCA1MCTL = UCBRS_3; // Modulation (오차 보정)
    UCA1CTL1 &= ~UCSWRST;
}

// --- SPI 속도 재설정 (24MHz 기준) ---
void SPI_Init(void) {
    UCB0CTL1 |= UCSWRST;
    UCB0CTL0 = UCMSB + UCMST + UCSYNC; // (UCCKPH 제거됨 유지)
    UCB0CTL1 |= UCSSEL_2; 
    // SMCLK(24MHz) / 24 = 1MHz SPI Clock
    UCB0BR0 = 24; 
    UCB0BR1 = 0;
    UCB0CTL1 &= ~UCSWRST;
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
            TA2CCR2 = PWM_DUTY_INIT; TA2CCTL2 = OUTMOD_7; // A_H PWM
            TA0CCTL3 = OUTMOD_0; TA0CCTL3 |= OUT;    // B_L ON
            break;
            
        case 1: // Step 2: A+ C-
            TA2CCR2 = PWM_DUTY_INIT; TA2CCTL2 = OUTMOD_7; // A_H PWM
            TB0CCTL5 = OUTMOD_0; TB0CCTL5 |= OUT;    // C_L ON
            break;
            
        case 2: // Step 3: B+ C-
            TA0CCR4 = PWM_DUTY_INIT; TA0CCTL4 = OUTMOD_7; // B_H PWM
            TB0CCTL5 = OUTMOD_0; TB0CCTL5 |= OUT;    // C_L ON
            break;
            
        case 3: // Step 4: B+ A-
            TA0CCR4 = PWM_DUTY_INIT; TA0CCTL4 = OUTMOD_7; // B_H PWM
            TA2CCTL1 = OUTMOD_0; TA2CCTL1 |= OUT;    // A_L ON
            break;
            
        case 4: // Step 5: C+ A-
            TB0CCR2 = PWM_DUTY_INIT; TB0CCTL2 = OUTMOD_7; // C_H PWM
            TA2CCTL1 = OUTMOD_0; TA2CCTL1 |= OUT;    // A_L ON
            break;
            
        case 5: // Step 6: C+ B-
            TB0CCR2 = PWM_DUTY_INIT; TB0CCTL2 = OUTMOD_7; // C_H PWM
            TA0CCTL3 = OUTMOD_0; TA0CCTL3 |= OUT;    // B_L ON
            break;
    }
}

// --- 타이머 초기화 (기본 설정만) ---
/*
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
*/
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
 /*
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
*/

/**
 * UART 모듈 (USCI_A1) 초기화 (115200bps @ 8MHz)
 */
 /*
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
*/

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
