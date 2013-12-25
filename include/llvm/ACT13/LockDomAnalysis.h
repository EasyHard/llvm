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
        bool doFinalization(Module &M);
    };
};

