/*******************************************************************************
 * Copyright (c) 1991, 2016 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include "AtomicSupport.hpp"

extern "C" {

#include "thrtypes.h"
#include "threaddef.h"
#include <stdio.h>

#define barrier() asm volatile("": : :"memory")

static __inline__ uint64_t fetch_and_store(omrthread_monitor_t monitor, uint64_t val);
static __inline__ uint64_t cmp_and_swap(omrthread_monitor_t monitor, uint64_t cmpval, uint64_t newval);

static __inline__ uint64_t
fetch_and_store(omrthread_monitor_t monitor, uint64_t val) 
{
    __asm__ volatile(
                "lock; xchgq %0, %1\n\t"
                : "+m" (monitor->tail), "+r" (val)
                : 
                : "memory", "cc");
    return val;
}

static __inline__ uint64_t
cmp_and_swap(omrthread_monitor_t monitor, uint64_t cmpval, uint64_t newval)
{
    uint64_t out;
    __asm__ volatile(
                "lock; cmpxchgq %2, %1"
                : "=a" (out), "+m" (monitor->tail)
                : "q" (newval), "0"(cmpval)
                : "cc");
    return out == cmpval;
}

void
omrthread_monitor_pin(omrthread_monitor_t monitor, omrthread_t self)
{
	VM_AtomicSupport::add(&monitor->pinCount, 1);
}

void
omrthread_monitor_unpin(omrthread_monitor_t monitor, omrthread_t self)
{
	VM_AtomicSupport::subtract(&monitor->pinCount, 1);
}

#if defined(OMR_THR_THREE_TIER_LOCKING)

/**
 * Spin on a monitor's spinlockState field until we can atomically swap out a value of SPINLOCK_UNOWNED
 * for the value SPINLOCK_OWNED.
 *
 * @param[in] self the current omrthread_t
 * @param[in] monitor the monitor whose spinlock will be acquired
 *
 * @return 0 on success, -1 on failure
 */
//intptr_t
//omrthread_spinlock_acquire(omrthread_t self, omrthread_monitor_t monitor)
//{
//	volatile uintptr_t *target = (volatile uintptr_t *)&monitor->spinlockState;
//	intptr_t result = -1;
//	uintptr_t oldState = J9THREAD_MONITOR_SPINLOCK_UNOWNED;
//	uintptr_t newState = J9THREAD_MONITOR_SPINLOCK_OWNED;
//	omrthread_library_t const lib = self->library;
//
//#if defined(OMR_THR_JLM)
//	J9ThreadMonitorTracing *tracing = NULL;
//	if (OMR_ARE_ALL_BITS_SET(lib->flags, J9THREAD_LIB_FLAG_JLM_ENABLED)) {
//		tracing = monitor->tracing;
//	}
//#endif /* OMR_THR_JLM */
//
//	uintptr_t spinCount3Init = monitor->spinCount3;
//	uintptr_t spinCount2Init = monitor->spinCount2;
//	uintptr_t spinCount1Init = monitor->spinCount1;
//
//#if defined(OMR_THR_SPIN_WAKE_CONTROL)
//	BOOLEAN spinning = TRUE;
//	if (OMRTHREAD_IGNORE_SPIN_THREAD_BOUND != lib->maxSpinThreads) {
//		if (monitor->spinThreads < lib->maxSpinThreads) {
//			VM_AtomicSupport::add(&monitor->spinThreads, 1);
//		} else {
//			spinCount1Init = 1;
//			spinCount2Init = 1;
//			spinCount3Init = 1;
//			spinning = FALSE;
//		}
//	}
//#endif /* defined(OMR_THR_SPIN_WAKE_CONTROL) */
//
//	uintptr_t spinCount3 = spinCount3Init;
//	uintptr_t spinCount2 = spinCount2Init;
//
//	for (; spinCount3 > 0; spinCount3--) {
//		for (spinCount2 = spinCount2Init; spinCount2 > 0; spinCount2--) {
//			/* Try to put 0 into the target field (-1 indicates free)'. */
//			if (oldState == VM_AtomicSupport::lockCompareExchange(target, oldState, newState, true)) {
//				result = 0;
//				VM_AtomicSupport::readBarrier();
//				goto update_jlm;
//			}
//			/* Stop spinning if adaptive spin heuristic disables spinning */
//			if (OMR_ARE_ALL_BITS_SET(monitor->flags, J9THREAD_MONITOR_DISABLE_SPINNING)) {
//				goto update_jlm;
//			}
//			VM_AtomicSupport::yieldCPU();
//			/* begin tight loop */
//			for (uintptr_t spinCount1 = spinCount1Init; spinCount1 > 0; spinCount1--)	{
//				VM_AtomicSupport::nop();
//			} /* end tight loop */
//		}
//#if defined(OMR_THR_YIELD_ALG)
//		omrthread_yield_new(spinCount3);
//#else /* OMR_THR_YIELD_ALG */
//		omrthread_yield();
//#endif /* OMR_THR_YIELD_ALG */
//	}
//
//update_jlm:
//#if defined(OMR_THR_JLM)
//	if (NULL != tracing) {
//		/* Add JLM counts atomically:
//		 * let m=spinCount3Init, n=spinCount2Init
//		 * let i=spinCount2, j=spinCount3
//		 *
//		 * after partial set of spins (0 == result),
//		 * yield_count += m-j, spin2_count += ((m-j)*n)+(n-i+1)
//		 *
//		 * after complete set of spins (-1 == result),
//		 * yield_count += m, spin2_count += m*n
//		 */
//		uintptr_t yield_count = spinCount3Init - spinCount3;
//		uintptr_t spin2_count = yield_count * spinCount2Init;
//		if (0 != yield_count) {
//			spin2_count += (spinCount2Init - spinCount2 + 1);
//		}
//		VM_AtomicSupport::add(&tracing->yield_count, yield_count);
//		VM_AtomicSupport::add(&tracing->spin2_count, spin2_count);
//	}
//#endif /* OMR_THR_JLM */
//
//#if defined(OMR_THR_SPIN_WAKE_CONTROL)
//	if (spinning && (OMRTHREAD_IGNORE_SPIN_THREAD_BOUND != lib->maxSpinThreads)) {
//		VM_AtomicSupport::subtract(&monitor->spinThreads, 1);
//	}
//#endif /* defined(OMR_THR_SPIN_WAKE_CONTROL) */
//
//	return result;
//}

///**
//  * Try to atomically swap out a value of SPINLOCK_UNOWNED from
//  * a monitor's spinlockState field for the value SPINLOCK_OWNED.
//  *
//  * @param[in] self the current omrthread_t
//  * @param[in] monitor the monitor whose spinlock will be acquired
//  *
//  * @return 0 on success, -1 on failure
//  */
//intptr_t
//omrthread_spinlock_acquire_no_spin(omrthread_t self, omrthread_monitor_t monitor)
//{
//	intptr_t result = -1;
//	volatile uintptr_t *target = (volatile uintptr_t *)&monitor->spinlockState;
//	uintptr_t oldState = J9THREAD_MONITOR_SPINLOCK_UNOWNED;
//	uintptr_t newState = J9THREAD_MONITOR_SPINLOCK_OWNED;
//	if (oldState == VM_AtomicSupport::lockCompareExchange(target, oldState, newState, true)) {
//		result = 0;
//		VM_AtomicSupport::readBarrier();
//	}
//	return result;
//}

/**
  * Atomically swap in a new value for a monitor's spinlockState.
  *
  * @param[in] monitor the monitor to modify
  * @param[in] newState the new value for spinlockState
  *
  * @return the previous value for spinlockState
  */
uintptr_t
omrthread_spinlock_swapState(omrthread_monitor_t monitor, uintptr_t newState)
{
	volatile uintptr_t *target = (volatile uintptr_t *)&monitor->spinlockState;
	/* If we are writing in UNOWNED, we are exiting the critical section, therefore
	 * have to finish up any writes.
	 */
	if (J9THREAD_MONITOR_SPINLOCK_UNOWNED == newState) {
		VM_AtomicSupport::writeBarrier();
	}
	uintptr_t oldState = VM_AtomicSupport::set(target, newState);
	/* If we entered the critical section, (i.e. we swapped out UNOWNED) then
	 * we have to issue a readBarrier.
	 */
	if (J9THREAD_MONITOR_SPINLOCK_UNOWNED == oldState) {
		VM_AtomicSupport::readBarrier();
	}
	return oldState;
}

intptr_t
omrthread_spinlock_acquire(omrthread_t self, omrthread_monitor_t monitor)
{
	intptr_t result = -1;
	volatile omrthread_mcs_node_t mcsNode = (omrthread_mcs_node_t)pool_newElement(self->mcsNodes->pool);
	//omrthread_mcs_node_t predecessor = (omrthread_mcs_node_t)VM_AtomicSupport::set((volatile uintptr_t *)monitor->tail, (uintptr_t)mcsNode);
	omrthread_mcs_node_t predecessor = (omrthread_mcs_node_t)fetch_and_store(monitor, (uint64_t)mcsNode);

	mcsNode->nextLock = NULL;
	if (NULL == self->mcsNodes->head) {
		self->mcsNodes->head = mcsNode;
		mcsNode->nextThread = NULL;
	} else {
		omrthread_mcs_node_t oldMCSHeadInThread = self->mcsNodes->head;
		self->mcsNodes->head = mcsNode;
		mcsNode->nextThread = oldMCSHeadInThread;
	}

//	printf("mcs lock enter: self=%p, monitor=%p, monitor->tail=%p, predecessor=%p, self->mcsNodes->head=%p, mcsNode=%p\n", self, monitor, monitor->tail, predecessor, self->mcsNodes->head, mcsNode);
	if (NULL != predecessor) {
		mcsNode->blocked = 1;
		predecessor->nextLock = mcsNode;

		barrier();

		for (uintptr_t spinCount3 = monitor->spinCount3; spinCount3 > 0; spinCount3--) {
			for (uintptr_t spinCount2 = monitor->spinCount2; spinCount2 > 0; spinCount2--) {
				if (0 == mcsNode->blocked) {
					monitor->spinlockState = J9THREAD_MONITOR_SPINLOCK_OWNED;
					result = 0;
					goto exit;
				}
				/* Stop spinning if adaptive spin heuristic disables spinning */
				if (OMR_ARE_ALL_BITS_SET(monitor->flags, J9THREAD_MONITOR_DISABLE_SPINNING)) {
					goto exit;
				}
				VM_AtomicSupport::yieldCPU();
				/* begin tight loop */
				for (uintptr_t spinCount1 = monitor->spinCount1; spinCount1 > 0; spinCount1--) {
					VM_AtomicSupport::nop();
				} /* end tight loop */
			}
#if defined(OMR_THR_YIELD_ALG)
			omrthread_yield_new(spinCount3);
#else /* OMR_THR_YIELD_ALG */
			omrthread_yield();
#endif /* OMR_THR_YIELD_ALG */
		}
	}

exit:
//	printf("mcs lock exit: self=%p, monitor=%p, head=%p, tail=%p, result=%ld\n", self, monitor, self->mcsNodes->head, monitor->tail, result);
	return result;
}

void
omrthread_mcs_unlock(omrthread_t self, omrthread_monitor_t monitor)
{
	omrthread_mcs_node_t latestMCSNodeInThread = self->mcsNodes->head;
//	printf("mcs unlock enter: self=%p, monitor=%p, mcsNode=%p, monitor->tail=%p, mcsNode->nextThread=%p\n", self, monitor, latestMCSNodeInThread, monitor->tail, latestMCSNodeInThread->nextThread);
	if (NULL == latestMCSNodeInThread->nextLock) {
		//if (latestMCSNodeInThread == (omrthread_mcs_node_t)VM_AtomicSupport::lockCompareExchange((volatile uintptr_t *)monitor->tail, (uintptr_t)latestMCSNodeInThread, (uintptr_t)NULL)) {
		if (cmp_and_swap(monitor, (uint64_t)latestMCSNodeInThread, 0)) {
			goto exit;
		}
		while (NULL == latestMCSNodeInThread->nextLock) {
			VM_AtomicSupport::yieldCPU();
		}
	}
	latestMCSNodeInThread->nextLock->blocked = 0;
exit:
	self->mcsNodes->head = self->mcsNodes->head->nextThread;
	latestMCSNodeInThread->nextLock = NULL;
	latestMCSNodeInThread->nextThread = NULL;
	latestMCSNodeInThread->blocked = 0;
	pool_removeElement(self->mcsNodes->pool, latestMCSNodeInThread);
//	printf("mcs unlock exit: self=%p, monitor=%p, head=%p, tail=%p\n", self, monitor, self->mcsNodes->head, monitor->tail);
}

/**
  * Try to atomically swap out a value of SPINLOCK_UNOWNED from
  * a monitor's spinlockState field for the value SPINLOCK_OWNED.
  *
  * @param[in] self the current omrthread_t
  * @param[in] monitor the monitor whose spinlock will be acquired
  *
  * @return 0 on success, -1 on failure
  */
intptr_t
omrthread_spinlock_acquire_no_spin(omrthread_t self, omrthread_monitor_t monitor)
{
	intptr_t result = -1;
	volatile omrthread_mcs_node_t mcsNode = (omrthread_mcs_node_t)pool_newElement(self->mcsNodes->pool);
	mcsNode->nextLock = NULL;
	mcsNode->blocked = 1;

//	printf("mcs trylock enter: self=%p, monitor=%p, monitor->tail=%p, self->mcsNodes->head=%p, mcsNode=%p\n", self, monitor, monitor->tail, self->mcsNodes->head, mcsNode);

	if (cmp_and_swap(monitor, 0, (uint64_t)mcsNode)) {
		monitor->spinlockState = J9THREAD_MONITOR_SPINLOCK_OWNED;
		if (NULL == self->mcsNodes->head) {
			self->mcsNodes->head = mcsNode;
			mcsNode->nextThread = NULL;
		} else {
			omrthread_mcs_node_t oldMCSHeadInThread = self->mcsNodes->head;
			self->mcsNodes->head = mcsNode;
			mcsNode->nextThread = oldMCSHeadInThread;
		}
		mcsNode->blocked = 0;
		result = 0;
		goto exit;
	}

	pool_removeElement(self->mcsNodes->pool, mcsNode);

exit:
//	printf("mcs trylock exit: self=%p, monitor=%p, head=%p, tail=%p, rc=%ld\n", self, monitor, self->mcsNodes->head, monitor->tail, result);
	return result;
}

#endif /* OMR_THR_THREE_TIER_LOCKING */

}
