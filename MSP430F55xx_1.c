#include <msp430.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

// --- 핀 정의 ---
#define ENC_A_PIN   BIT6 // P1.6
#define ENC_B_PIN   BIT6 // P6.6
#define ENC_I_PIN   BIT7 // P2.7 (Index)

// --- 전역 변수 ---
volatile int32_t g_encoder_count = 0;
volatile int32_t g_captured_count = 0;
volatile uint8_t g_result_ready = 0;

// --- 함수 선언 ---
void SystemClock_Init(void);
void SetVCoreUp(uint8_t level);
void UART_Init_115200(void);
void UART_PrintInt32(int32_t n);
void UART_PrintString(char *str);
void Encoder_Init(void);

// --- 메인 함수 ---
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;

    // 1. 클럭 24MHz 설정 (115200bps를 위해 필수)
    SystemClock_Init(); 
    
    // 2. UART 115200bps 설정
    UART_Init_115200();
    
    // 3. 엔코더 & 인덱스 핀 설정
    Encoder_Init();

    __enable_interrupt(); // 인터럽트 활성화

    UART_PrintString("\r\n=== Encoder Resolution Test (Index Based) ===\r\n");
    UART_PrintString("Speed: 115200 bps\r\n");
    UART_PrintString("Action: Rotate motor multiple times.\r\n");
    UART_PrintString("Waiting for Index signal...\r\n");

    while (1) {
        // 인덱스 인터럽트에서 한 바퀴 측정이 끝나면 플래그를 세움
        if (g_result_ready) {
            UART_PrintString("One Rev Count: ");
            UART_PrintInt32(g_captured_count);
            UART_PrintString("\r\n");
            
            g_result_ready = 0; // 플래그 클리어
        }
    }
}

// --- 엔코더 및 인덱스 초기화 ---
void Encoder_Init(void) {
    // 1. A상 (P1.6) - 인터럽트 사용
    P1DIR &= ~ENC_A_PIN; P1REN |= ENC_A_PIN; P1OUT |= ENC_A_PIN;
    P1IES &= ~ENC_A_PIN; P1IFG &= ~ENC_A_PIN; P1IE |= ENC_A_PIN;

    // 2. B상 (P6.6) - 입력만
    P6DIR &= ~ENC_B_PIN; P6REN |= ENC_B_PIN; P6OUT |= ENC_B_PIN;

    // 3. Index상 (P2.7) - 인터럽트 사용!
    P2DIR &= ~ENC_I_PIN; P2REN |= ENC_I_PIN; P2OUT |= ENC_I_PIN;
    
    // Index 신호가 Low Active인지 High Active인지에 따라 설정 (보통 Falling Edge 권장)
    P2IES |= ENC_I_PIN;  // Falling Edge 감지 (High -> Low)
    P2IFG &= ~ENC_I_PIN;
    P2IE  |= ENC_I_PIN;  // 인터럽트 활성화
}

// --- P1 인터럽트 (카운팅) ---
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void) {
    if (P1IFG & ENC_A_PIN) {
        // A상, B상 읽기
        uint8_t B_val = (P6IN & ENC_B_PIN) ? 1 : 0;
        
        if (P1IES & ENC_A_PIN) { // Rising Edge
            if (B_val == 0) g_encoder_count++;
            else            g_encoder_count--;
            P1IES |= ENC_A_PIN; // 다음 Edge 설정 (Rising 유지? A상 로직 확인)
            // *주의*: 지난번 로직 유지 (Rising/Falling 모두 잡아서 2체배)
            // 만약 4체배를 원하면 A, B 모두 인터럽트 걸어야 함.
            // P1.6만으로는 최대 2체배(2048)가 한계입니다.
            // 일단 'Rising'만 잡아서 1체배(1024)인지 확인하는 게 가장 깔끔할 수도 있습니다.
            // 여기서는 Rising/Falling 모두 잡는 로직 적용 (예상값: 2048)
        } else { // Falling Edge
            if (B_val == 1) g_encoder_count++;
            else            g_encoder_count--;
            P1IES &= ~ENC_A_PIN; 
        }
        P1IFG &= ~ENC_A_PIN;
    }
}

// --- P2 인터럽트 (Index 감지 및 리셋) ---
#pragma vector=PORT2_VECTOR
__interrupt void Port_2(void) {
    if (P2IFG & ENC_I_PIN) {
        // 1. 현재까지의 카운트 캡처
        g_captured_count = g_encoder_count;
        
        // 2. 카운터 리셋
        g_encoder_count = 0;
        
        // 3. 메인 루프에 알림
        // (단, 0인 경우는 처음 켜자마자 Index 건드린 경우일 수 있으니 제외 가능)
        if (g_captured_count != 0) {
            g_result_ready = 1; 
        }

        P2IFG &= ~ENC_I_PIN; // 플래그 클리어
    }
}

// --- UART & System Clock (24MHz / 115200bps) ---
void SetVCoreUp(uint8_t level) { 
    // (이전과 동일한 PMM 승압 코드)
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
    UCSCTL0 = 0x0000; UCSCTL1 = DCORSEL_7; UCSCTL2 = FLLD_0 + 732; // 24MHz
    __bic_SR_register(SCG0);
    __delay_cycles(250000);
    do {
        UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG);
        SFRIFG1 &= ~OFIFG;
    } while (SFRIFG1 & OFIFG);
}

void UART_Init_115200(void) {
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 |= UCSWRST;
    UCA1CTL1 |= UCSSEL_2; // SMCLK 24MHz
    // 24,000,000 / 115200 = 208.333
    UCA1BR0 = 208; UCA1BR1 = 0;
    UCA1MCTL = UCBRS_3; // Modulation
    UCA1CTL1 &= ~UCSWRST;
}

void UART_PrintString(char *str) {
    while(*str) { while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = *str++; }
}

void UART_PrintInt32(int32_t n) {
    if (n < 0) { UART_PrintString("-"); n = -n; }
    char buf[12]; int i=0;
    if (n==0) { UART_PrintString("0"); return; }
    while(n>0) { buf[i++] = (n%10)+'0'; n/=10; }
    while(i>0) { while (!(UCA1IFG & UCTXIFG)); UCA1TXBUF = buf[--i]; }
}