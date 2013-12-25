/**
 * LockDomAnalysis is a context-sensitive, flow-sensitive
 * inter-procedure analysis try to figure out which locks must be
 * hold before each IR instruction.
 *
 * Currently, the LockDomAnalysis has two major flaws
 * 1. It is based on FlowtoAnalysis result, which limit its usage.
 * 2. To break the cycle in callgraph, it simply assume locks always
 * acquire and release in same function.
 *
 * `LocakDomAnalysis::dom` provides a context-insensitive result which
 * contains result of the intersection of all possible callstring,
 * to retrive context-sensitive result, use the callsite result in `dom`
 * and function `analyzeCallSite`
 **/
#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ACT13/PTGraph.h"
#include "llvm/Support/CallSite.h"
#include <vector>
#include <map>


using namespace llvm;

namespace ACT {
    struct LockDomAnalysis : public ModulePass {
        static char ID;
        LockDomAnalysis();
        // locks that must be hold before instruction v
        std::map<Instruction*, std::set<Value*>> dom;
        virtual bool runOnModule(Module &M);
        // We don't modify the program, so we preserve all analyses
        virtual void getAnalysisUsage(AnalysisUsage &AU) const;
        virtual void releaseMemory();
        std::map<Instruction*, std::set<Value*>> analyzeCallSite(CallSite* CS, Module& M);
        bool doFinalization(Module &M);
    };
};

