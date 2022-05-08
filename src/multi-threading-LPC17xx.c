/*
===============================================================================
 Name        : context-switching.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __USE_CMSIS
#include "LPC17xx.h"
#endif

#include <cr_section_macros.h>

#define CONTROL_THREAD_MODE_Pos 0
#define CONTROL_THREAD_MODE_Msk (0x1 << CONTROL_THREAD_MODE_Pos)  // 0 - Privilege, 1 - User state

#define CONTROL_STACK_Pos 1
#define CONTROL_STACK_Msk (0x1 << CONTROL_STACK_Pos) // 0 - MSP, 1 - PSP

#define MAX_SRAM_ADDRESS 0x10007FFF // Cortex-M3 with 32 kB SRAM

#define STACK_SIZE 0x800 // 2 kB

#define FRIST_THREAD_STACK_POINTER (MAX_SRAM_ADDRESS - STACK_SIZE)

#define EXC_RETURN 0xFFFFFFF0

#define RETURN_THREAD_MODE_WITH_PSP 0xD

#define DEFAULT_PSR 0x01000000

typedef struct os_thread {
	// handled by manually
	uint32_t r11;
	uint32_t r10;
	uint32_t r9;
	uint32_t r8;
	uint32_t r7;
	uint32_t r6;
	uint32_t r5;
	uint32_t r4;

	uint32_t SP;

	struct os_thread *next_thread;
} os_thread;

typedef struct {
	// handled by NVIC
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t LR;
	uint32_t PC;
	uint32_t xPSR;
} thread_stack;

static os_thread *current_os_thread;

void led_pin_init(void) {
	LPC_PINCON->PINSEL1 &= ~(0x3 << 12);
	//LPC_PINCON->PINMODE1 |= (0x20 << 12);

	LPC_GPIO0->FIODIR |= (0x1 << 22);
	LPC_GPIO0->FIOMASK &= ~(0x1 << 22);
	LPC_GPIO0->FIOSET |= (0x1 << 22);
}

void timer0_count_init(void) {
	LPC_SC->PCLKSEL0 |= (0x1 << 2);

	LPC_TIM0->PR = 8;
	LPC_TIM0->TCR |= (0x1 << 0);
	LPC_TIM0->TC = 0;
	LPC_TIM0->MR0 = 0x2faf08;
	LPC_TIM0->MCR |= (0x3 << 0);

	NVIC_EnableIRQ(TIMER0_IRQn);
}

void enable_systick_irq(void) {
	SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

void disable_systick_irq(void) {
	SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
}

void pending_pendsv(void) {
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void toggling_led(void) {
	if (LPC_GPIO0->FIOPIN & (0x1 << 22)) {
		LPC_GPIO0->FIOCLR |= (0x1 << 22);
	} else {
		LPC_GPIO0->FIOSET |= (0x1 << 22);
	}
}

void Timer0_Handler(void){
	LPC_TIM0->IR |= (1 << 0);

	toggling_led();
}

void Systick_Handler(void){

	//uint32_t ctrl = __get_CONTROL();

	// __set_CONTROL();
	// __get_PSP(void);
	// __set_PSP(uint32_t topOfProcStack);
	// __get_MSP(void);
	// __set_MSP(uint32_t topOfMainStack);∂


	// __set_CONTROL(CONTROL_STACK_Msk);

	//thread_stack *access_stack = (thread_stack*)(__get_MSP() - sizeof(thread_stack));

	pending_pendsv();
}

void Pendsv_Handler(void) {
	static bool not_first;

	if(not_first) {
		thread_stack *access_stack = (thread_stack*)(__get_PSP());

		current_os_thread->SP = __get_PSP();

		current_os_thread = current_os_thread->next_thread;

	} else {
		not_first = true;
	}

	__set_PSP(current_os_thread->SP);

	thread_stack *access_stack2 = (thread_stack*)(__get_PSP());
}

void thread1(void) {
	while(1){
		__NOP();
	}
}

void thread2(void) {
	while(1){
		__NOP();
	}
}

void del_thread(void) {
	pending_pendsv();
}

void new_thread(void (*thread_ptr)(void)) {
	static os_thread *first_thread;
	static os_thread *last_thread;
	static uint32_t number_of_threads;

	disable_systick_irq();

	os_thread *new_thread = malloc(sizeof(os_thread));

	memset(new_thread, 0, sizeof(os_thread));

	if(!current_os_thread){
		current_os_thread = new_thread;
		first_thread = new_thread;
		last_thread = new_thread;
	}

	uint32_t sp = FRIST_THREAD_STACK_POINTER - (number_of_threads * STACK_SIZE);

	thread_stack *access_stack = (thread_stack*)(sp - sizeof(thread_stack) - 3);
	access_stack->PC = (uint32_t) thread_ptr;
	access_stack->LR = (uint32_t) &del_thread;
	access_stack->xPSR = DEFAULT_PSR;

	new_thread->SP = sp - sizeof(thread_stack);

	last_thread->next_thread = new_thread;
	last_thread = new_thread;
	new_thread->next_thread = first_thread;

	number_of_threads++;

	enable_systick_irq();
}

int main(void) {
	led_pin_init();
	timer0_count_init();

	new_thread(thread1);
	new_thread(thread2);

	SysTick_Config(1000);

	__enable_irq();

	__set_PSP(current_os_thread->SP);

	__set_CONTROL(CONTROL_THREAD_MODE_Msk | CONTROL_STACK_Msk);

    while(1) {
        __NOP();
    }
    return 0 ;
}
