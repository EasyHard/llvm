/**
 * ShareAnalysis is based on FlowtoAnalysis. Analyzing which memory locations
 * may be shared between thread and which instructions should be protected.
 *
 * It solves the problem by analyzing memory locations one thread accessed and
 * locations that IR instruciton accessed after the thread created. The sharing
 * locations are locations that show up in both.
 *
 * The result are in ShareAnalysis::sharingLocation. Each Value* in the map
 * is a shared memory location, and the Instruction list are IR instructions
 * that need to be protected (based on the result of FlowtoAnalysis).
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
    struct ShareAnalysis : public ModulePass {
        static char ID;
        ShareAnalysis();
        std::map<Value*, std::vector<Instruction*>> sharingLocation;
        virtual bool runOnModule(Module &M);
        // We don't modify the program, so we preserve all analyses
        virtual void getAnalysisUsage(AnalysisUsage &AU) const;
        std::vector<Instruction*> accessFrom(Instruction *inst);
        std::vector<Instruction*> accessFrom(BasicBlock *inst);
        void handleInst(std::vector<Instruction*>& accessSet, std::list<BasicBlock*>&blockList, Instruction &inst);
        std::set<PTNode*> accessingLocation(std::vector<Instruction*>& list);
        virtual void releaseMemory();
        bool doFinalization(Module &M);
    };
};

