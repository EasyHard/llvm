/**
 * A context-sensitive, flow-insensitive point-to analysis,
 * which will analyze pointers that are used in one
 * function may point-to heap, stack locations, even the stack locations
 * are not in this function's.
 * It provides some kind of context-sensitive result. By the C2GMap, refer
 * to below comments. For context-insensitive users, check `f2g`.
 *
 * Please notes that this analysis is *very imcomplete*, it could not correctly
 * handle cyclic callgraph.
 **/

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
        // for every values in this function may point to (both global, heap and
        // stack variables in other functions)
        // The CallSite are for context-sensitive analysis usage. If you need
        // more context-sensitive than that, sorry for not helping.
        typedef std::map<CallSite*, PTGraph*> C2GMap;
        C2GMap c2g;
        // This is a context-insensitive analysis result.
        std::map<Function*, PTGraph*> f2g;
        // immedieate result
        std::map<CallSite*, PTGraph*> csInput;
        std::map<CallSite*, std::pair<PTGraph*, PTNode*>> csResult;
        // The above functions are implement details, refer to .cpp please.
        FlowtoAnalysis();
        virtual bool runOnModule(Module &M);
        virtual void releaseMemory();
        void getAnalysisUsage(AnalysisUsage &AU) const;
        PTGraph *graphForCallSite(const CallSite& CS) ;
        // Take callsite and graph as input, generate new graph in csResult
        // and update csInput for called function.
        /* PTNode *analyze(Function *func, PTGraph* flowinto, bool* added = NULL); */
        bool analyze(CallSite *csp, PTGraph* flowinto); 
        void cleanupForFunc(Function *funcp, PTGraph *flowinto);
        bool runInstruction(PTGraph* graph, Instruction& inst);

        Function* mainp;
    };
};

