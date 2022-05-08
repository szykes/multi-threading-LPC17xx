/*
 * irq-handlers.h
 *
 *  Created on: May 1, 2022
 *      Author: szikes.adam
 */

#ifndef IRQ_HANDLERS_H_
#define IRQ_HANDLERS_H_

void Pendsv_Handler(void);
void Systick_Handler(void);
void Timer0_Handler(void);

#endif /* IRQ_HANDLERS_H_ */
