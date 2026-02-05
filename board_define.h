/*
 * board_define.h
 *
 * User Defined Pin Mapping (Based on provided image/text)
 * MSP430F5529 <-> DRV8305
 */

#ifndef BOARD_DEFINE_H_
#define BOARD_DEFINE_H_

#include <msp430.h>


// --- 하드웨어 핀 정의 (사용자 정보 반영) ---
// Encoder A: P1.6 (Interrupt OK)
#define ENC_A_DIR   P1DIR
#define ENC_A_IN    P1IN
#define ENC_A_REN   P1REN
#define ENC_A_OUT   P1OUT
#define ENC_A_IE    P1IE
#define ENC_A_IES   P1IES
#define ENC_A_IFG   P1IFG
#define ENC_A_PIN   BIT6

// Encoder B: P6.6 (No Interrupt -> Polling in ISR)
#define ENC_B_DIR   P6DIR
#define ENC_B_IN    P6IN
#define ENC_B_REN   P6REN
#define ENC_B_OUT   P6OUT
#define ENC_B_PIN   BIT6

// Encoder I (Index): P2.7
#define ENC_I_DIR   P2DIR
#define ENC_I_IN    P2IN
#define ENC_I_REN   P2REN
#define ENC_I_OUT   P2OUT
#define ENC_I_PIN   BIT7

// ============================================================================
// [1] Communication Pins (SPI & UART)
// ============================================================================
// SPI (USCI_B0) - User Map: P3.0, P3.1, P3.2
// P3.2가 SCLK이므로, 하드웨어 점퍼가 P3.2(J4.35) <-> DRV_CLK(J2.3)에 연결되어 있어야 함
#define SPI_SEL_PORT        P3SEL
#define SPI_DIR_PORT        P3DIR
#define SPI_SIMO_PIN        BIT0    // P3.0 (SDI)
#define SPI_SOMI_PIN        BIT1    // P3.1 (SDO)
#define SPI_CLK_PIN         BIT2    // P3.2 (SCLK)

// UART (USCI_A1) - COM6 Debugging
#define UART_TX_PIN         BIT4    // P4.4
#define UART_RX_PIN         BIT5    // P4.5

// ============================================================================
// [2] DRV8305 Control Signals (GPIO)
// ============================================================================
// Chip Select (SCS) -> P2.0 [User Defined]
#define DRV_CS_PORT         P2OUT
#define DRV_CS_DIR          P2DIR
#define DRV_CS_PIN          BIT0    // P2.0

// Enable Gate (ENGATE) -> P2.6 [User Defined]
#define DRV_EN_PORT         P2OUT
#define DRV_EN_DIR          P2DIR
#define DRV_EN_PIN          BIT6    // P2.6

// Wake (WAKE) -> P2.3 [User Defined]
#define DRV_WAKE_PORT       P2OUT
#define DRV_WAKE_DIR        P2DIR
#define DRV_WAKE_PIN        BIT3    // P2.3

// Fault (nFAULT) -> P3.4 [User Defined]
#define DRV_FAULT_PORT      P3IN
#define DRV_FAULT_DIR       P3DIR
#define DRV_FAULT_PIN       BIT4    // P3.4

// ============================================================================
// [3] Analog Sensing (ADC12)
// ============================================================================
// Voltage Sense (P6.0, P6.1, P6.2)
#define PIN_VSEN_A          BIT0    // P6.0 (A0)
#define PIN_VSEN_B          BIT1    // P6.1 (A1)
#define PIN_VSEN_C          BIT2    // P6.2 (A2)

// Current Sense (P6.4, P7.0, P6.5)
#define PIN_ISEN_A          BIT4    // P6.4 (A4)
#define PIN_ISEN_B          BIT0    // P7.0 (A12) - Port 7 주의!
#define PIN_ISEN_C          BIT5    // P6.5 (A5)

// ADC Channels for Software
#define ADC_CH_VSEN_A       ADC12INCH_0
#define ADC_CH_VSEN_B       ADC12INCH_1
#define ADC_CH_VSEN_C       ADC12INCH_2
#define ADC_CH_ISEN_A       ADC12INCH_4
#define ADC_CH_ISEN_B       ADC12INCH_12
#define ADC_CH_ISEN_C       ADC12INCH_5

// ============================================================================
// [4] PWM Output Pins (Referencing previous turns)
// ============================================================================
// PWM 핀은 이번 리스트에 없었지만, 기존 설정을 유지합니다.
// Phase A (P2.4, P2.5), Phase B (P1.4, P1.5), Phase C (P7.4, P3.5 - Jumper)
// ※ 주의: P3.5가 Phase C Low로 사용되므로, 다른 핀과 겹치지 않는지 확인 필요
#define PWM_A_H_PORT    P2OUT
#define PWM_A_H_PIN     BIT5
#define PWM_A_L_PORT    P2OUT
#define PWM_A_L_PIN     BIT4

#define PWM_B_H_PORT    P1OUT
#define PWM_B_H_PIN     BIT5
#define PWM_B_L_PORT    P1OUT
#define PWM_B_L_PIN     BIT4

#define PWM_C_H_PORT    P7OUT
#define PWM_C_H_PIN     BIT4
#define PWM_C_L_PORT    P3OUT
#define PWM_C_L_PIN     BIT5

#endif /* BOARD_DEFINE_H_ */
