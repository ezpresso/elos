#ifndef ARCH_INTERRUPT_H
#define ARCH_INTERRUPT_H

#define IDT_TYPE_TRAP 	0xF /* 32bit trap */
#define IDT_TYPE_INTR	0xE /* 32bit interrupt (disable interrupts) */

#define EXCEPTION_NUM	32
#define INT_NUM		256 /* Total number of interrupts */

#define INT_DE 		0 /* divide by zero */
#define INT_DB		1 /* debug */
#define INT_NMI		2
#define INT_BP		3 /* breakpoint */
#define INT_OF		4 /* overflow */
#define INT_BR		5 /* Bound range exceeded */
#define INT_UD		6 /* invalid opcode */
#define INT_NM		7 /* device not available */
#define INT_DF		8 /* double fault */
#define INT_TS		10 /* invalid tss */
#define INT_NP		11 /* segment not present */
#define INT_SS		12 /* stack segment fault */
#define INT_GP		13 /* general protection fault */
#define INT_PF		14 /* page fault */
#define INT_AC		17 /* alignment check */	
#define INT_MC		18 /* machine check */
#define INT_XM		19 /* SMID exception*/
#define INT_VE		20 /* virtualization exception */
#define INT_SX		30 /* security exception */
#define INT_DEV_START	32
#define INT_SYSCALL	128
#define INT_DEV_END	238
#define INT_APIC_TIMER	239

/*
 * Interrupts for the LVT-entries of the local APIC.
 */
#define APIC_LOCAL_INTS 	240
#define 	INT_APIC_ERROR 		(APIC_LOCAL_INTS + 0)
#define 	INT_APIC_THERM 		(APIC_LOCAL_INTS + 1)
#define 	INT_APIC_CMC		(APIC_LOCAL_INTS + 2)
#define INT_IPI_INTS		(APIC_LOCAL_INTS + 3)
#define 	INT_IPI_BITMAP		(INT_IPI_INTS + 0)
#define		INT_IPI_INVLPG		(INT_IPI_INTS + 1)
#define		INT_IPI_INVLTLB		(INT_IPI_INTS + 2)
#define INT_NMI_PANIC		255
#define INT_APIC_SPURIOUS	255

#ifndef __ASSEMBLER__
/* interrupt.S */
extern void asmlinkage int_return(void);
extern void asmlinkage syscall_idt(void);
extern void asmlinkage lapic_spurious(void);
#endif

#endif