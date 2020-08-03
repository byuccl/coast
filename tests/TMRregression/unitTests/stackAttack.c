/*
 * stackAttack.c
 * This unit test specifically formulated to have a predictable
 *  call stack so we can try some techniques to protect against
 *  SDCs in stack frames.
 * Mark all the functions so they are not inlined.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>


// print some messages
#if defined(__has_feature)
# if __has_feature(shadow_call_stack)
/***************************** Shadow Call Stack ******************************/
#   pragma message ("Using Shadow Call Stack protection")

    #ifdef __x86_64__
    ///////////////////////////// x86 run-time /////////////////////////////
    // No run-time is provided for the x86 implementation.
    #include <asm/prctl.h>
    #include <sys/prctl.h>
    // for some reason, compiler still complains about implicit declaration
    extern int arch_prctl(int code, unsigned long *addr);
    
    // Set aside space for the gs register to save return addresses in.
    #define SHADOW_STACK_SIZE 512
    uint64_t shadowStack[SHADOW_STACK_SIZE];

    // Run some code before main() starts
    // https://stackoverflow.com/a/8713662/12940429
    void premain(void) __attribute__ ((constructor))
            __attribute__((no_sanitize("shadow-call-stack")));

    void premain(void) {
        // We need to set the `gs` register.
        // https://stackoverflow.com/a/59282564/12940429
        arch_prctl(ARCH_SET_GS, &shadowStack[SHADOW_STACK_SIZE - 1]);
    }
    #endif

# endif

# if (__SSP_ALL__ == 3) || (__SSP_STRONG__ == 2) || (__SSP__ == 1)
/************************** Stack Protector (Canary) **************************/
    #ifdef __ARM_EABI__
    //////////////////////////// ARM32 run-time ////////////////////////////
    // automatic for x86, not for ARM clang baremetal
    // https://embeddedartistry.com/blog/2020/05/18/implementing-stack-smashing-protection-for-microcontrollers-and-embedded-artistrys-libc/
    // also this, but it's not as good
    // https://antoinealb.net/programming/2016/06/01/stack-smashing-protector-on-microcontrollers.html

    // did the user set it already?
    #ifndef STACK_CHK_GUARD_VALUE
        // stack guard value - 32 or 64 bit?
        #if (UINTPTR_MAX == UINT32_MAX)
        #define STACK_CHK_GUARD 0xdeadbeef
        #else
        #define STACK_CHK_GUARD 0xdeadbeef8badf00d
        #endif
    #endif

    // this is the canary value
    uintptr_t __attribute__((weak)) __stack_chk_guard = 0;

    // randomly generated canary value?
    // https://github.com/gcc-mirror/gcc/blob/master/libssp/ssp.c

    // callback can be overwritten by user
    __attribute__((weak)) uintptr_t __stack_chk_guard_init(void) {
        return STACK_CHK_GUARD;
    }

    // pre-main initialization of stack guard value
    static void __attribute__((constructor,no_stack_protector)) __construct_stk_chk_guard()
    {
        if (__stack_chk_guard == 0) {
            __stack_chk_guard = __stack_chk_guard_init();
        }
    }

    #include <stdlib.h>
    // this gets called at a mismatch
    void __stack_chk_fail(void) __attribute__((weak, noreturn));
    void __stack_chk_fail(void) {
        printf("Stack smashed! Aborting...\n");
        abort();
    }
    #endif
# endif

#endif


/********************************* Functions **********************************/
// simple functions to test nested stack frames
__attribute__((noinline))
int func3(uint32_t arg3) {
    return arg3 + 3;
}

__attribute__((noinline))
int func2(uint32_t arg2) {
    return func3(arg2) + 2;
}

__attribute__((noinline))
int func1(uint32_t arg1) {
    return func2(arg1) + 1;
}


#ifdef FORTIFY_SOURCE
/*
 * Function that tries to overwrite the stack return address.
 * Simple mistake to make, forgetting to count the null terminator.
 */
void unsafeCopy(void) {
    char buf[12];
    strcpy(buf, "Hello there!");
    printf("%s\n", buf);
    return;
}
#endif


int main() {
    int ret = 0;
    uint32_t x = 42;
    // expected result: (((42 + 3) + 2) + 1) = 48
    uint32_t result = func1(x);

    #ifdef FORTIFY_SOURCE
    // buffer test
    unsafeCopy();
    #endif

    if (result != 48) {
        printf("Error, got %u, expected 48!\n", result);
        ret = -1;
    } else {
        printf("Success!\n");
    }

    #ifdef __QEMU_SIM
    // have to spin forever instead of actually returning
    while (1);
    #endif
    return ret;
}


/********************************* x86 notes **********************************/
/*
 * normal disassembly of func2:
 * 0x00000000004004f0 <+0>:	    push   %rbp
 * 0x00000000004004f1 <+1>:	    mov    %rsp,%rbp
 * 0x00000000004004f4 <+4>:	    sub    $0x10,%rsp
 * 0x00000000004004f8 <+8>:	    mov    %edi,-0x4(%rbp)
 * 0x00000000004004fb <+11>:	mov    -0x4(%rbp),%edi
 * 0x00000000004004fe <+14>:	callq  0x4004e0 <func3>
 * 0x0000000000400503 <+19>:	add    $0x2,%eax
 * 0x0000000000400506 <+22>:	add    $0x10,%rsp
 * 0x000000000040050a <+26>:	pop    %rbp
 * 0x000000000040050b <+27>:	retq   
 */

/*
 * with -fsanitize=shadow-call-stack
 * (removed in LLVM 9.0, for future reference)
 * 0000000000400570 <func2>:
 *   400570:       4c 8b 14 24             mov    (%rsp),%r10
 *   400574:       4d 31 db                xor    %r11,%r11
 *   400577:       65 49 83 03 08          addq   $0x8,%gs:(%r11)
 *   40057c:       65 4d 8b 1b             mov    %gs:(%r11),%r11
 *   400580:       65 4d 89 13             mov    %r10,%gs:(%r11)
 *   400584:       55                      push   %rbp
 *   400585:       48 89 e5                mov    %rsp,%rbp
 *   400588:       48 83 ec 10             sub    $0x10,%rsp
 *   40058c:       89 7d fc                mov    %edi,-0x4(%rbp)
 *   40058f:       8b 7d fc                mov    -0x4(%rbp),%edi
 *   400592:       e8 b9 ff ff ff          callq  400550 <func3>
 *   400597:       83 c0 02                add    $0x2,%eax
 *   40059a:       48 83 c4 10             add    $0x10,%rsp
 *   40059e:       5d                      pop    %rbp
 *   40059f:       4d 31 db                xor    %r11,%r11
 *   4005a2:       65 4d 8b 13             mov    %gs:(%r11),%r10
 *   4005a6:       65 4d 8b 12             mov    %gs:(%r10),%r10
 *   4005aa:       65 49 83 2b 08          subq   $0x8,%gs:(%r11)
 *   4005af:       4c 39 14 24             cmp    %r10,(%rsp)
 *   4005b3:       75 01                   jne    4005b6 <func2+0x46>
 *   4005b5:       c3                      retq
 *   4005b6:       0f 0b                   ud2
 *   4005b8:       0f 1f 84 00 00 00 00    nopl   0x0(%rax,%rax,1)
 *   4005bf:       00
 * see https://releases.llvm.org/7.0.1/tools/clang/docs/ShadowCallStack.html
 */

/*
 * with -fstack-protector-all
 * 0000000000400580 <func2>:
 *   400580:       55                      push   %rbp
 *   400581:       48 89 e5                mov    %rsp,%rbp
 *   400584:       48 83 ec 10             sub    $0x10,%rsp
 *   400588:       64 48 8b 04 25 28 00    mov    %fs:0x28,%rax
 *   40058f:       00 00
 *   400591:       48 89 45 f8             mov    %rax,-0x8(%rbp)
 *   400595:       89 7d f4                mov    %edi,-0xc(%rbp)
 *   400598:       8b 7d f4                mov    -0xc(%rbp),%edi
 *   40059b:       e8 a0 ff ff ff          callq  400540 <func3>
 *   4005a0:       83 c0 02                add    $0x2,%eax
 *   4005a3:       64 48 8b 0c 25 28 00    mov    %fs:0x28,%rcx
 *   4005aa:       00 00
 *   4005ac:       48 8b 55 f8             mov    -0x8(%rbp),%rdx
 *   4005b0:       48 39 d1                cmp    %rdx,%rcx
 *   4005b3:       75 06                   jne    4005bb <func2+0x3b>
 *   4005b5:       48 83 c4 10             add    $0x10,%rsp
 *   4005b9:       5d                      pop    %rbp
 *   4005ba:       c3                      retq
 *   4005bb:       e8 70 fe ff ff          callq  400430 <__stack_chk_fail@plt>
 */


/********************************* ARM notes **********************************/
/*
 * normal disassembly of func2:
 * 0x001004cc <+0>:	    push	{r11, lr}
 * 0x001004d0 <+4>:	    mov	r11, sp
 * 0x001004d4 <+8>:	    sub	sp, sp, #8
 * 0x001004d8 <+12>:	str	r0, [sp, #4]
 * 0x001004dc <+16>:	ldr	r0, [sp, #4]
 * 0x001004e0 <+20>:	bl	0x1004b4 <func3>
 * 0x001004e4 <+24>:	add	r0, r0, #2
 * 0x001004e8 <+28>:	mov	sp, r11
 * 0x001004ec <+32>:	pop	{r11, pc}
 */

/*
 * ShadowCallStack doesn't support 32-bit ARM, only aarch64.
 * We would need a 64-bit ARM simulator to test this.
 */

/*
 * with -fstack-protector-all
 * 001005ac <func2>:
 *   1005ac:       e92d4800        push    {fp, lr}
 *   1005b0:       e1a0b00d        mov     fp, sp
 *   1005b4:       e24dd008        sub     sp, sp, #8
 *   1005b8:       e3041034        movw    r1, #16436      ; 0x4034
 *   1005bc:       e3401011        movt    r1, #17
 *   1005c0:       e5911000        ldr     r1, [r1]
 *   1005c4:       e58d1004        str     r1, [sp, #4]
 *   1005c8:       e58d0000        str     r0, [sp]
 *   1005cc:       e59d0000        ldr     r0, [sp]
 *   1005d0:       ebffffe0        bl      100558 <func3>
 *   1005d4:       e2800002        add     r0, r0, #2
 *   1005d8:       e3041034        movw    r1, #16436      ; 0x4034
 *   1005dc:       e3401011        movt    r1, #17
 *   1005e0:       e5911000        ldr     r1, [r1]
 *   1005e4:       e59d2004        ldr     r2, [sp, #4]
 *   1005e8:       e0411002        sub     r1, r1, r2
 *   1005ec:       e3510000        cmp     r1, #0
 *   1005f0:       1a000000        bne     1005f8 <func2+0x4c>
 *   1005f4:       ea000000        b       1005fc <func2+0x50>
 *   1005f8:       ebffffcf        bl      10053c <__stack_chk_fail>
 *   1005fc:       e1a0d00b        mov     sp, fp
 *   100600:       e8bd8800        pop     {fp, pc}
 */
