#include <msp430.h>
#include <stdint.h>     // uint8_t, uint16_t 사용
#include <stdio.h>      // vsnprintf 사용
#include <stdarg.h>     // va_list 사용
#include "board_define.h" 

// ============================================================================
// [함수 선언부 (Prototypes)]
// ============================================================================
void BSP_Init(void);
void SPI_Init(void);
void UART_Init(void);
void UART_Printf(char *format, ...);

uint16_t DRV_ReadReg(uint8_t addr);
void DRV_WriteReg(uint8_t addr, uint16_t data);
void UART_PrintHex16(uint16_t value);
void Test_Connection_GPIO(void);
void GPIO_Alive_Test(void);

// ============================================================================
// [메인 함수 (Main)]
// ============================================================================
int main(void) {
    BSP_Init();     // 하드웨어 핀 설정
    SPI_Init();     // SPI 모듈 설정
    UART_Init();    // UART 모듈 설정

    // 전원 안정화 대기
    __delay_cycles(1000000); 
    UART_Printf("\r\n=== System Booting... ===\r\n");

    // 1. DRV8305 깨우기 (Wake Up Sequence)
    // WAKE 핀 High -> 1ms 이상 대기
    DRV_WAKE_PORT |= DRV_WAKE_PIN;
    __delay_cycles(80000); // 약 10ms 대기 (8MHz 기준)
    
    // EN_GATE 활성화
    DRV_EN_PORT |= DRV_EN_PIN;
    __delay_cycles(8000); 

    UART_Printf("[DRV8305] Waking up...\r\n");

    // 2. 무한 루프: 상태 확인
    while (1) {
        // Status Register 0x01 읽기 (Warning Register)
        uint16_t status = DRV_ReadReg(0x01);
        
        UART_Printf("Reg 0x01: ");
        UART_PrintHex16(status);  // 전용 함수로 출력 (무조건 0x0000 형식으로 나옴)
        UART_Printf("\r\n");
        
        // Test_Connection_GPIO();
        // GPIO_Alive_Test();

        // UART_Printf("Status Reg(0x05): 0x%04X\r\n", status);

        // if (status == 0xFFFF) {
        //     UART_Printf(" -> Error: SPI MISO stuck High (Check Power/Connection).\r\n");
        // } else if (status == 0x0000) {
        //     UART_Printf(" -> Success: Communication OK. System Normal.\r\n");
        // } else {
        //     UART_Printf(" -> Warning: Fault detected or Value Read.\r\n");
        // }
        
        // P1.0 LED 토글 (동작 확인용)
        P1OUT ^= BIT0; 
        
        // 1초 대기
        __delay_cycles(8000000); 
    }
}

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

// [하드웨어 연결 테스트용 코드]
// SPI를 쓰지 않고, 핀을 1초마다 High/Low로 바꿉니다.
// 멀티미터나 오실로스코프로 '도착지점(DRV8305 핀)'을 찍어보세요.

void Test_Connection_GPIO(void) {
    // 1. 핀을 GPIO 출력으로 설정
    P3DIR |= BIT0; // SDI (P3.0) -> Output
    P3DIR |= BIT2; // SCLK (P3.2) -> Output (여기가 J2.3까지 가는지 확인!)
    P2DIR |= BIT6; // SCS (P2.6) -> Output (여기가 J2.8까지 가는지 확인!)

    UART_Printf("=== GPIO Toggle Test Mode ===\r\n");

    while(1) {
        // High 상태
        P3OUT |= BIT0; // SDI = 3.3V
        P3OUT |= BIT2; // SCLK = 3.3V
        P2OUT |= BIT6; // SCS = 3.3V
        UART_Printf("ALL PINS HIGH (Check 3.3V)\r\n");
        __delay_cycles(4000000); // 0.5초 대기

        // Low 상태
        P3OUT &= ~BIT0; // SDI = 0V
        P3OUT &= ~BIT2; // SCLK = 0V
        P2OUT &= ~BIT6; // SCS = 0V
        UART_Printf("ALL PINS LOW (Check 0V)\r\n");
        __delay_cycles(4000000); // 0.5초 대기
    }
}

void GPIO_Alive_Test(void) {
    WDTCTL = WDTPW | WDTHOLD; // 와치독 정지

    // 1. SPI/Peripheral 기능 모두 해제 (순수 GPIO 모드)
    P3SEL &= ~(BIT0 | BIT1 | BIT2); 
    
    // 2. 방향을 출력으로 설정
    P3DIR |= (BIT0 | BIT2); // P3.0(SIMO 위치), P3.2(SCLK 위치)

    while(1) {
        // High
        P3OUT |= (BIT0 | BIT2);
        __delay_cycles(400000); // 딜레이
        
        // Low
        P3OUT &= ~(BIT0 | BIT2);
        __delay_cycles(400000); // 딜레이
    }
}
