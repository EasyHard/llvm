/**
 * Detecting race based on all the analysis.
 *
 * A race is considered as a memory location access without same locks hold.
 * i.e. Each shared memory location need to be accessed by having at least
 * one same lock held. RaceDetector checks intersection of
 * all accessing instructions' lockdom.
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
    struct RaceDetector : public ModulePass {
        static char ID;
        RaceDetector();
        virtual bool runOnModule(Module &M);
        // We don't modify the program, so we preserve all analyses
        virtual void getAnalysisUsage(AnalysisUsage &AU) const;
        virtual void releaseMemory();
        bool doFinalization(Module &M);
        std::vector<CallSite*> getCallSiteList(Function* funcp, Module& M);
    };
};

