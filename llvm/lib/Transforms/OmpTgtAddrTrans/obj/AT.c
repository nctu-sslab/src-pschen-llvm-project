#include <stdint.h>

struct ATTableTy {
  uintptr_t HstPtrBegin;
  uintptr_t HstPtrEnd;
  uintptr_t TgtPtrBegin;
  intptr_t bias;
};

#pragma omp declare target
// Less mul/div ALU??
/*void *AddrTrans(void* addr, struct ATTableTy* table) {
    int size = table[0].HstPtrBegin;
    uintptr_t ret = (uintptr_t) 0;
    uintptr_t addr_int = (intptr_t) addr;
    for (int i = 1; i <= size; i++) {
        if (addr_int >= table[i].HstPtrBegin && addr_int < table[i].HstPtrEnd) {
            ret = addr_int - table[i].HstPtrBegin + table[i].TgtPtrBegin;
            break;
        }
    }
    return (void*)ret;
}*/
// TODO Edge case, Compare perf use a switch on size
// Binary search version
void *AddrTrans(void* addr, struct ATTableTy* table) {
    int size = table[0].HstPtrBegin;
    uintptr_t ret = (uintptr_t) 0;
    uintptr_t addr_int = (intptr_t) addr;
    int head = 1, end = size + 1;
    while (head < end) {
        int mid = (head + end) << 1;
        if (addr_int >= table[mid].HstPtrBegin) {
            if (addr_int < table[mid].HstPtrEnd) {
                ret = addr_int - table[mid].HstPtrBegin + table[mid].TgtPtrBegin;
                break;
            }
            head = mid+1;
        } else {
            end = mid;
        }
    }
    return (void*)ret;
}
#pragma omp end declare target

/* Compile me with

 "/home/pschen/llvm/thesis/build-Debug/bin/clang-9" -cc1 -triple x86_64-unknown-linux-gnu -emit-llvm-bc -emit-llvm-uselists -disable-free -main-file-name func.c -mrelocation-model static -mthread-model posix -fmath-errno -masm-verbose -mconstructor-aliases -munwind-tables -fuse-init-array -target-cpu x86-64 -dwarf-column-info -debugger-tuning=gdb -momit-leaf-frame-pointer -resource-dir /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0 -I /home/pschen/llvm/thesis/build-Debug/include -I /home/pschen/sslab/src-pschen/omp_offloading/include -D N=10 -cxx-isystem /home/pschen/sslab/omp_offloading/include -internal-isystem /usr/local/include -internal-isystem /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0/include -internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -internal-isystem /usr/local/include -internal-isystem /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0/include -internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -O2 -fdebug-compilation-dir /home/pschen/sslab/src-pschen/test/MultiFile/tmp -ferror-limit 19 -fmessage-length 0 -fopenmp -fobjc-runtime=gcc -fdiagnostics-show-option -fcolor-diagnostics -vectorize-loops -vectorize-slp -fopenmp-targets=nvptx64 -faddrsig -o AT.bc -x c AT.c


"/home/pschen/llvm/thesis/build-Debug/bin/clang-9" -cc1 -triple nvptx64 -aux-triple x86_64-unknown-linux-gnu -S -disable-free -main-file-name func.c -mrelocation-model pic -pic-level 2 -mthread-model posix -mdisable-fp-elim -no-integrated-as -fuse-init-array -mlink-builtin-bitcode /usr/local/cuda-10.1/nvvm/libdevice/libdevice.10.bc -target-feature +ptx64 -target-sdk-version=10.1 -mlink-builtin-bitcode /home/pschen/llvm/thesis/build-Debug/lib/libomptarget-nvptx-sm_60.bc -target-cpu sm_60 -dwarf-column-info -debugger-tuning=gdb -resource-dir /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0 -internal-isystem /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0/include/openmp_wrappers -include __clang_openmp_math_declares.h -I /home/pschen/llvm/thesis/build-Debug/include -I /home/pschen/sslab/src-pschen/omp_offloading/include -D N=10 -cxx-isystem /home/pschen/sslab/omp_offloading/include -internal-isystem /usr/local/include -internal-isystem /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0/include -internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -internal-isystem /usr/local/include -internal-isystem /home/pschen/llvm/thesis/build-Debug/lib/clang/9.0.0/include -internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -O2 -fno-dwarf-directory-asm -fdebug-compilation-dir /home/pschen/sslab/src-pschen/test/MultiFile/tmp -ferror-limit 19 -fmessage-length 0 -fopenmp -fobjc-runtime=gcc -fdiagnostics-show-option -fcolor-diagnostics -vectorize-loops -vectorize-slp -fopenmp-is-device -fopenmp-host-ir-file-path /tmp/func-d7cba9.bc -o AT.s -x c AT.c

"/usr/local/cuda-10.1/bin/ptxas" -m64 -O2 --gpu-name sm_60 --output-file AT.cubin AT.s -c
*/


