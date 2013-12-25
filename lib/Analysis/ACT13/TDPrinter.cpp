#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "dsa/DataStructure.h"
using namespace llvm;
namespace ACT {
    struct TDPrinter : public ModulePass {
        static char ID;
        TDPrinter() : ModulePass(ID) {}
        virtual bool runOnModule(Module &M) {
            errs() << "What's wrong?\n";
            //EquivBUDataStructures &td = getAnalysis<EquivBUDataStructures>();
            EQTDDataStructures &td = getAnalysis<EQTDDataStructures>();
            td.print(errs(), &M);
            return false;
        }

        // We don't modify the program, so we preserve all analyses
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<EQTDDataStructures>();
        }
    };
}

char ACT::TDPrinter::ID = 0;
static RegisterPass<ACT::TDPrinter> X("TDPrinter", "TDPrinter Pass", false, false);

