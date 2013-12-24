/**
 * This pass will replace all pthread_create_thread as a normal function call
 * By doing this, flow-insensitive analysis like FlowtoAnalysis could just
 * analysis the replaced IR and get a result that respect to the thread
 * semantic.
 **/
#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include <vector>

using namespace llvm;

namespace ACT {
    struct ReplaceFunc : public ModulePass {
        static char ID;
        ReplaceFunc();
        std::vector<std::pair<CallInst*, CallSite*>> replacedCallInstList;
        virtual bool runOnModule(Module &M);
        // We don't modify the program, so we preserve all analyses
        virtual void getAnalysisUsage(AnalysisUsage &AU) const;
        virtual void releaseMemory();
        bool doFinalization(Module &M);
    };
};

