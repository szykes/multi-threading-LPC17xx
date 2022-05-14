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

#define MIDDLE_SRAM_ADDRESS 0x10008000

#define THREAD_MEM_SIZE 0x800 // 2 kiB

#define FRIST_THREAD_STACK_POINTER (MIDDLE_SRAM_ADDRESS - THREAD_MEM_SIZE)

#define DEFAULT_PSR 0x01000000

#define MAX_NO_THREADS 2

static const uint32_t kEmptyThread = 0;

typedef struct os_thread {
	uint32_t SP;
} os_thread;

// The 'packed' makes sure the elements are in a row in the memory.
__attribute__ ((packed))
typedef struct {
	// handled by manually
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;

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

static volatile uint32_t thread_idx;
static volatile uint32_t max_threads;
static volatile os_thread os_thread_barn[MAX_NO_THREADS];

static volatile uint32_t main_func_sp;
static volatile uint32_t *previous_sp = &main_func_sp;
static volatile uint32_t *next_sp;

static void led_pin_init(void) {
	LPC_PINCON->PINSEL1 &= ~(0x3 << 12);
	//LPC_PINCON->PINMODE1 |= (0x20 << 12);

	LPC_GPIO0->FIODIR |= (0x1 << 22);
	LPC_GPIO0->FIOMASK &= ~(0x1 << 22);
	LPC_GPIO0->FIOSET |= (0x1 << 22);
}

static void timer0_count_init(void) {
	LPC_SC->PCLKSEL0 |= (0x1 << 2);

	LPC_TIM0->PR = 8;
	LPC_TIM0->TCR |= (0x1 << 0);
	LPC_TIM0->TC = 0;
	LPC_TIM0->MR0 = 0x2faf08;
	LPC_TIM0->MCR |= (0x3 << 0);

	NVIC_EnableIRQ(TIMER0_IRQn);
}

static void enable_systick_irq(void) {
	SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

static void disable_systick_irq(void) {
	SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
}

static void pending_pendsv(void) {
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

static void pend_svc(void) {
	__ASM volatile ("svc 0");
}

static void toggling_led(void) {
	if (LPC_GPIO0->FIOPIN & (0x1 << 22)) {
		LPC_GPIO0->FIOCLR |= (0x1 << 22);
	} else {
		LPC_GPIO0->FIOSET |= (0x1 << 22);
	}
}

void TIMER0_IRQHandler(void){
	LPC_TIM0->IR |= (1 << 0);

	toggling_led();
}

void HardFault_Handler(void)
{ while(1) {}
}

void SVC_Handler(void) {
	// The very first running of this handler interrupts the main() function and this SP is useless at
		// this point. Let's skip it.
	static bool skip_first;

	if(skip_first) {
		previous_sp = &os_thread_barn[thread_idx].SP;

		thread_idx++;
		if(thread_idx >= max_threads){
			thread_idx = 0;
		}
	} else {
		skip_first = true;
	}

	next_sp = &os_thread_barn[thread_idx].SP;

	pending_pendsv();
}

void SysTick_Handler(void){
	// SysTick gives the heartbeat for the context switching. However, this handler cannot do the
	// context switch in real because it would mess up the stacks.

	previous_sp = &os_thread_barn[thread_idx].SP;

	thread_idx++;
	if(thread_idx >= max_threads){
		thread_idx = 0;
	}

	next_sp = &os_thread_barn[thread_idx].SP;

	pending_pendsv();
}

// The 'naked' removes the compiler added code from this functions. This is important because it can mess
// up the content of registers.
__attribute__ ((naked))
void PendSV_Handler(void) {
	// PendSV has the lowest priority because this should be executed lastly if there are more triggered
	// IRQs.

	__ASM volatile(
		// Disable interrupt
		"cpsid i             \n"

		// Store the PSP this was SP of interrupted thread. This threads goes to idle.
		"mrs r0, psp         \n"
		// Push the R4-R11 registers because they are pushed by the CPU.
		"stmdb r0!,{r4-r11}  \n"

		// Store the PSP in the os_thread via the previous_sp.
		"ldr r2, =previous_sp\n"
		"ldr r1, [r2]        \n"
		"str r0, [r1]        \n"

		// Load the PSP of the next thread.
		"ldr r2, =next_sp    \n"
		"ldr r1, [r2]        \n"
		"ldr r0, [r1]        \n"

		// Pop the R4-R11 registers.
		"ldmia r0!, {r4-r11} \n"
		// Set the PSP.
		"msr psp, r0         \n"

		// When the CPU returns to normal execution here we can set how the returning should is processed.
		// In the current case the normal execution will be in thread mode with PSP.
		"ldr r0, =0xFFFFFFFD \n"

		// Enable interrupt.
		"cpsie i             \n"

		"bx	r0                 "
	);
}

static unsigned int factorial(unsigned int n)
{
    if (n == 0)
        return 1;
    return n * factorial(n - 1);
}

void thread1(uint32_t arg) {
	// I filled the registers in fill_thread_stack_memory() to verify their contents can survive
	// a context switch. Yes, they will survive.
	while(1){
		int c = factorial(arg);
		if (c != 3628800){
			break;
		}
	}
}

void thread2(uint32_t arg) {
	while(1){
		int c = factorial(arg);
		if (c != 6){
			break;
		}
	}
}

void del_thread(void) {
	// thread has ended, do whatever you want here :) for example: pend the SVC to perform context switch.
	while (1) {}
}

static void fill_thread_stack_memory(os_thread *empty_os_thread, void (*thread_ptr)(uint32_t), uint32_t arg) {
	// I hardcoded where the SP can start in the memory map together with the size of memory
	// for each thread.
	// Yeah, this is lazy...
	uint32_t sp = FRIST_THREAD_STACK_POINTER - (max_threads * THREAD_MEM_SIZE);

	// The address of struct starts at the lower address meanwhile the stack grows in other direction. So,
	// I need to put the last element of struct at the starting address of stack.
	thread_stack *access_stack = (thread_stack*)(sp - sizeof(thread_stack));

	// It fills the stack with given values. When the PendSV_Handler() loads the register contents from
	// stack, it will read these. So, this is where I can manipulate how the thread starts.
	access_stack->PC = (uint32_t) thread_ptr;

	// The del_thread() will be called, when the thread ends.
	access_stack->LR = (uint32_t) &del_thread;
	access_stack->xPSR = DEFAULT_PSR;
	access_stack->r0 = arg;

	// No need to do anything with the other registers, they will be replaced anyway.

	// The os_thread execution starts with popping the values from stack.
	empty_os_thread->SP = sp - sizeof(thread_stack);
}

void new_thread(void (*thread_ptr)(uint32_t), uint32_t arg) {
	if(thread_ptr == NULL) {
		return;
	}

	disable_systick_irq();

	int i;
	os_thread *empty_os_thread = NULL;
	for(i = 0; i < (MAX_NO_THREADS); i++) {
		if(os_thread_barn[i].SP == kEmptyThread) {
			empty_os_thread = &os_thread_barn[i];
			break;
		}
	}

	if(empty_os_thread == NULL) {
		return;
	}

	fill_thread_stack_memory(empty_os_thread, thread_ptr, arg);

	max_threads++;

	enable_systick_irq();
}

int main(void) {
	led_pin_init();
	timer0_count_init();

	NVIC_SetPriority(PendSV_IRQn, 0xFF);
	NVIC_SetPriority(SVCall_IRQn, 0xE0);
	NVIC_SetPriority(SysTick_IRQn, 0x00);

	new_thread(thread1, 10);
	new_thread(thread2, 3);

	SysTick_Config(1000);

	__enable_irq();

	pend_svc();

	// Using the PSP is recommended here because this is how the PendSV_Handler() can be lightweight.
	// The PendSV_Handler() uses 'psp' only. If I used MSP here, the PendSV_Handler() would require
	// a branch for handling MSP once at the first run.
	//__set_PSP(os_thread_barn[thread_idx].SP);
	//__set_CONTROL(CONTROL_THREAD_MODE_Msk | CONTROL_STACK_Msk);

	// I cannot start the os_thread execution here because it would override the previously set
	// link register that points to del_thread(). The new_thread() prepares the stacks of each os_thread.
    while(1) {
        __NOP();
    }
    return 0 ;
}
