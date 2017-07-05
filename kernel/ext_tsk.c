/*
 * Copyright (C) 2007,2008,2009,2010
 * 256TECH Co., Ltd.
 * Masahiro Sakamoto (m-sakamoto@users.sourceforge.net)
 *
 * This file is part of URIBO.
 *
 * URIBO is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * URIBO is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with URIBO; see the file COPYING and COPYING.LESSER.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include "kernel.h"

/*
 * ext_tsk system call (never return)
 */
void ext_tsk()
{
    T_TCB *tcb;
    
    /*
     * start critical section
     */
    _KERNEL_DIS();
    /*
     * check context
     */
    if (_KERNEL_CHK_ISLD(_kernel_sts)) {
#ifdef _KERNEL_ASSERT
        _KERNEL_ASSERT_EXT_TSK();
#else
        _KERNEL_PANIC();
#endif
    }
    /*
     * remove from ready queue
     */
    tcb = (T_TCB *)_kernel_cur;
    _kernel_deq((T_LNK *)tcb);
    /*
     * stop overrun handler
     */
    if (_kernel_ovrcnt)
        _kernel_ovrcnt[tcb->id - 1] = 0;

#ifdef _KERNEL_MTX
    /*
     * unlock all mutexes
     */
    (*_kernel_unl_amtx_fp)(tcb);
#endif /* _KERNEL_MTX */

    /*
     * restart if act queueing
     */
    if (tcb->act) {
        tcb->act--;
        _KERNEL_SET_SP(_kernel_tim_sp);     /* change SP to temporary */
        _kernel_act(tcb, tcb->ctsk->exinf); /* never return */
    }
    /*
     * make DORMANT state
     */
    else {
        tcb->sts = TTS_DMT;
        _kernel_cur = NULL;     /* purge context */
        _kernel_highest();      /* dispatch, never return */
    }
    for (;;) ;                  /* delete tail of func */
}

/* end */
