#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/ACT13/PTGraph.h"
#include <map>
#include <vector>

using namespace llvm;

namespace ACT {
    struct FlowtoAnalysis : public ModulePass {
        static char ID;
        // function and callsite of this function, return a point to graph
        // for every values in this function may point to (both global, and
        // heap in other )
        typedef std::map<CallSite*, PTGraph*> C2GMap;
        C2GMap c2g;
        FlowtoAnalysis();
        virtual bool runOnModule(Module &M);
        virtual void releaseMemory();
        void getAnalysisUsage(AnalysisUsage &AU) const;
        PTGraph *graphForCallSite(const CallSite& CS) ;
        PTGraph *analyze(Function *func, PTGraph* flowinto);
        bool runInstruction(PTGraph* graph, Instruction& inst);
    };
};

