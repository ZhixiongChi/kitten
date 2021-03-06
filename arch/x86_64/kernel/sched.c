#include <lwk/kernel.h>
#include <lwk/smp.h>
#include <lwk/task.h>
#include <arch/i387.h>

struct task_struct *
__arch_context_switch(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->arch.thread;
	struct thread_struct *next = &next_p->arch.thread;
	id_t cpu = this_cpu;
	struct tss_struct *tss = &per_cpu(tss, cpu);

	/* Update TSS */
	tss->rsp0 = next->rsp0;

	/* Switch DS and ES segment registers */
	asm volatile("mov %%es,%0" : "=m" (prev->es));
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es);
	asm volatile("mov %%ds,%0" : "=m" (prev->ds));
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	/* Load FS and GS segment registers (used for thread local storage) */
	{
		unsigned int fsindex;
		asm volatile("movl %%fs,%0" : "=r" (fsindex));
		if (unlikely(fsindex | next->fsindex | prev->fs)) {
			loadsegment(fs, next->fsindex);
			if (fsindex)
				prev->fs = 0;
		}
		if (next->fs)
			wrmsrl(MSR_FS_BASE, next->fs);
		prev->fsindex = fsindex;
	}
	{
		unsigned int gsindex;
		asm volatile("movl %%gs,%0" : "=r" (gsindex));
		if (unlikely(gsindex | next->gsindex | prev->gs)) {
			load_gs_index(next->gsindex);
			if (gsindex)
				prev->gs = 0;
		}
		if (next->gs)
			wrmsrl(MSR_KERNEL_GS_BASE, next->gs);
		prev->gsindex = gsindex;
	}

	/* Update the CPU's PDA (per-CPU data area) */
	prev->userrsp = read_pda(oldrsp);
	write_pda(oldrsp, next->userrsp);
	write_pda(pcurrent, next_p);
	write_pda(kernelstack, (vaddr_t)next_p + TASK_SIZE - PDA_STACKOFFSET);

	/* save and restore floating-point state */
	fpu_save_state(prev_p);
	fpu_restore_state(next_p);

	return prev_p;
}

void
arch_idle_task_loop_body(int irqenable)
{
#ifdef CONFIG_IDLE_HALT
	/* Issue HALT instruction,
	 * which should put CPU in a lower power mode */
	if (irqenable) {
		safe_halt();
	} else {
		halt();
	}
#endif

#ifdef CONFIG_IDLE_MONITOR_MWAIT
	if (irqenable) {
		__monitor((void *)get_current(), 0, 0);
		smp_mb();
		// mwait arg1: 0x00 = C1, 0x10 = C2, 0x20 = C3
		__sti_mwait(0x20, 0);
	} else {
		__monitor((void *)get_current(), 0, 0);
		smp_mb();
		__mwait(0, 0);
	}
#endif
}
