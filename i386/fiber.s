# Copyright (c) 2018-2020 The EFramework Project
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

.equ FIBER_STACK_UPPER_OFFSET, 8
.equ FIBER_STACK_PTR_OFFSET, 16
.equ FIBER_STATUS_OFFSET, 20
.equ FIBER_PARENT_OFFSET, 24
.equ FIBER_SCHED_OFFSET, 28

.equ SCHED_CURRENT_FIBER_OFFSET, 0

.equ FIBER_STATUS_EXITED, 0
.equ FIBER_STATUS_INITED, 1

.global ef_fiber_internal_swap
.global ef_fiber_internal_init

.text

ef_fiber_internal_swap:
mov %esp,%eax
push %edx
push %ecx
push %ebx
push %ebp
push %esi
push %edi
pushfl
mov 8(%eax),%edx
mov %esp,(%edx)
mov 4(%eax),%esp
mov 12(%eax),%eax
_ef_fiber_restore:
popfl
pop %edi
pop %esi
pop %ebp
pop %ebx
pop %ecx
pop %edx
ret

_ef_fiber_exit:
pop %edx
mov $FIBER_STATUS_EXITED,%ecx
mov %ecx,FIBER_STATUS_OFFSET(%edx)
mov FIBER_PARENT_OFFSET(%edx),%ecx
mov FIBER_SCHED_OFFSET(%edx),%edx
mov %ecx,SCHED_CURRENT_FIBER_OFFSET(%edx)
mov FIBER_STACK_PTR_OFFSET(%ecx),%esp
jmp _ef_fiber_restore

ef_fiber_internal_init:
push %ebp
mov %esp,%ebp
push %edx
push %ecx
mov 8(%ebp),%edx
mov $FIBER_STATUS_INITED,%eax
mov %eax,FIBER_STATUS_OFFSET(%edx)
mov %edx,%ecx
mov FIBER_STACK_UPPER_OFFSET(%edx),%edx
mov %ecx,-4(%edx)
mov 16(%ebp),%eax
mov %eax,-8(%edx)
lea _ef_fiber_exit,%eax
mov %eax,-12(%edx)
mov 12(%ebp),%eax
mov %eax,-16(%edx)
xor %eax,%eax
mov %eax,-20(%edx)
mov %eax,-24(%edx)
mov %eax,-28(%edx)
mov %edx,-32(%edx)
mov %eax,-36(%edx)
mov %eax,-40(%edx)
mov %eax,-44(%edx)
mov %edx,%eax
sub $44,%eax
pop %ecx
pop %edx
mov %ebp,%esp
pop %ebp
ret

