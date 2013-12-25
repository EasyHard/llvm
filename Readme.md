# VURD - Very Useless Race Detector

VURD (pronounce like `verd`), or Very Useless Race Detector, is a course project
in [Advanced Compiler Techniques][]. The course is instructed by
[Prof.Liu Xianhua][] and the project is written by me (Easyhard@Github). It is a
static race analyzer runs on LLVM IR, try to find memory accessing without
properly locking. Though it is based on LLVM IR, currently only pthread's mutex
operations are supported.

## Where the code locate
Most of the repo is from LLVM. The headers of VURD are in `include/llvm/ACT13` and
sources are in `lib/Analysis/ACT13`. Feel free to check out and laugh at. >_<

## How to use it?
It is implemented as a set of LLVM passes. You need to clone this repo, compile
the LLVM source tree as usual, then, generate a LLVM IR bit-code by compiling C
source like


    #include <pthread.h>
    pthread_mutex_t lock1, lock2;
    int *gp;
    int count1 = 0;
    int count2 = 0;
    void atomic_inc(pthread_mutex_t *lock, int *count) {
        pthread_mutex_lock(lock);
        *count = *count + 1;
        pthread_mutex_unlock(lock);
    }
    
    void *thread3(void *b) {
        int *countp = (int*) b;
        while (1) {
            atomic_inc(&lock1, &count1);
            atomic_inc(&lock2, &count2);
            atomic_inc(&lock1, countp);
        }
        return NULL;
    }
    
    void work(int *c) {
        int n = *c;
        for (int i = 0; i < n; i++)
            *c += i;
    }
    void *thread2(void *b) {
        int *countp = (int*) b;
        // safe. lock2 for count2
        pthread_mutex_lock(&lock2);
        count2++;
        pthread_mutex_unlock(&lock2);
        // race. lock2 with count1
        pthread_mutex_lock(&lock2);
        count1++;
        pthread_mutex_unlock(&lock2);
    
        pthread_mutex_t *l = &lock1;
        if (count1) {
            l = &lock2;
        }
        // l may points to two locks.
        pthread_mutex_lock(l);
        count1++;
        pthread_mutex_unlock(l);
    
        while (1) {
            // safe. use lock1 for countp
            pthread_mutex_lock(&lock1);
            work(countp);
            pthread_mutex_unlock(&lock1);
        }
        return NULL;
    }
    
    int main(void) {
        pthread_t t1, t2, t3;
        count1 = 10;
        count2 = 20;
        int share_count = 0;
        pthread_create(&t2, NULL, thread2, (void*)&share_count);
        pthread_create(&t3, NULL, thread3, (void*)&share_count);
        while (!share_count) {
            ;
        }
        return 0;
    }


    $ clang -c -emit-llvm -O1 -o example.bc example.c

You can also disassemble the LLVM IR bit-code by

    llvm-dis < example.bc

Then you can use opt to load the race detecting pass

     PATH_TO_LLVM_BUILD/Debug+Asserts/bin/opt -load \
      PATH_TO_LLVM_BUILD/Debug+Asserts/lib/RACE.so \
      -RaceDetector example.bc

After *lots* useless logs (Yes, that's a feature!), it will reach something like

    [RD] oops, please check   store i32 %14, i32* @count1, align 4, !tbaa !0in func thread2
    [RD] value @count1 = global i32 0, align 4 may be accessed without proper lock
    [RD] Check for the following instruciton: 
    [RD]  store i32 %14, i32* @count1, align 4, !tbaa !0in func thread2
    [RD]  store i32 %3, i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %2 = load i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %7 = load i32* @count1, align 4, !tbaa !0in func thread2
    [RD]  store i32 %8, i32* @count1, align 4, !tbaa !0in func thread2
    [RD]  %10 = load i32* @count1, align 4, !tbaa !0in func thread2
    [RD]  %13 = load i32* @count1, align 4, !tbaa !0in func thread2
    
    [RD] oops, please check   %.pr = load i32* %share_count, align 4, !tbaa !0in func main
    [RD] value   %share_count = alloca i32, align 4 may be accessed without proper lock
    [RD] Check for the following instruciton: 
    [RD]  store i32 %12, i32* %c, align 4, !tbaa !0in func work
    [RD]  store i32 %3, i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %2 = load i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %1 = load i32* %c, align 4, !tbaa !0in func work
    [RD]  %c.promoted = load i32* %c, align 4, !tbaa !0in func work
    [RD]  %.pr = load i32* %share_count, align 4, !tbaa !0in func main
    [RD]  store i32 %3, i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %2 = load i32* %count, align 4, !tbaa !0in func atomic_inc
    [RD]  %.pr = load i32* %share_count, align 4, !tbaa !0in func main

This is, the sharing memory location and accessing instructions.

## Approach
Here I will explain the underlay idea of VURD.

VURD try to figure out which memory location are sharing (accessing at the same
time) in different threads. And such accesses of the same memory location should
have at least one same lock held in any execute path. If it is not, VURD reports
a race for the memory location.

And flaws on the above analysis are major source of why VURD is *very useless*.

### Point-to Analysis
For distinguishing different memory location and determine memory locations
instructions are accessing, a point-to analysis are surely needed. And to
support function like `atomic_inc` in the above example, which is a common pattern
in multi-thread programming, the point-to analysis need to be context-sensitive,
and could distinguishing result of different context, instead of merging
them. Sadly, LLVM does not have a context-sensitive point-to analysis(alias
analysis, in LLVM's term). Though LLVM's manual claims its have ds-aa in project
poolalloc, but in LLVM's mail-list, the author and maintainer of poolalloc
committed ds-aa has broken for a long time.

Another requirement of the point-to analysis is that it should be
flow-insensitive, rather than flow-sensitive. It is because the key of race is
the order are not sure, or do not matter, which fits the semantic of
flow-insensitive.

So in VURD I implement a very *simple* point-to analysis, which is partly(but
not fully, of course) context-sensitive, and insensitive of anything else. But a
point-to analysis is still to hard. For keeping it *silly*, the point-to
analysis in VURD currently can not handling cyclic call graph. It simply drops
met function when analyzing.

### Lock Dominate Analysis
Part of the race detection is to determine whether and which locks are hold
before memory accesses. It is kind like the classic dominate basic block analysis
but need to untrack the dominate relationship if there may be a path could
release the lock or the lock is not unique based on the result of point-to
analysis.

But again, for supporting multi-thread-programming pattern, like `work` function
in the above example, This analysis need to be context-sensitive and also
inter-procedure. And again, for a simpler life, VURD currently assume locks will
be hold and release in the same function. i.e. the summary of any function is that
it will do nothing to any locks.

## Limitation
Currently, VURD could only analyze single source file and it always assume there
is a `main` as entry point. And you may also need to concern about the flaws
mentioned above.

## Future Improvement - Toward Useful Race Detector

### Data Structure Analysis
Good news for the project, but bad news to me. A few days ago I realized that the author of poolalloc
was saying that `ds-aa` is rotted, but poolalloc itself is still actively
maintained. It provides result of [Data Structure Analysis][]. If it could
provide the point-to analysis and lock-DOM analysis, VURD will make a big step
to a useful race detector.

### Memory Management
Currently I do not release memory allocated for the point-to analysis graph. It
need to fix.

### multi-entry point support
As the title. Try to analyze source file without a `main` entry point.

## Why I publish it?
VURD is indeed, as its name, *very useless*. But you could see what 2K lines
code could provide, with help of the great architecture of LLVM and *C++11*. Yes,
I used *C++11* in the project. *auto* and *stl* are very very helpful for
coding in a much simpler way.

[Prof.Liu Xianhua]: http://mprc.pku.edu.cn/~liuxianhua/

[Data Structure Analysis]: http://rw4.cs.uni-saarland.de/teaching/spa10/papers/dsa.pdf

[Advanced Compiler Techniques]: http://mprc.pku.edu.cn/~liuxianhua/ACT13/

