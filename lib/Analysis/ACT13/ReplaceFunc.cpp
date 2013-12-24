#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/Module.h"
#include <vector>
#include <deque>

#include "llvm/ACT13/ReplaceFunc.h"


using namespace llvm;

namespace ACT {
    char ReplaceFunc::ID = 0;

    ReplaceFunc::ReplaceFunc() : ModulePass(ID) {}

    void ReplaceFunc::releaseMemory() {
    }

    bool ReplaceFunc::doFinalization(Module &M) {
        return true;
    }
    
    bool ReplaceFunc::runOnModule(Module &M) {
        for (auto &func : M) {
            for (auto &BB : func) {
                for (auto &inst : BB) {
                    // errs() << "checking : " << inst << "\n";
                    CallSite* CS = new CallSite(&inst);
                    if (!!(*CS)) {
                        if (CS->getCalledFunction()->getName() == "pthread_create") {
                            errs() << "replacing: " << *CS->getInstruction() << "\n";
                            CallInst* newinst = CallInst::Create(CS->getArgument(2),
                                ArrayRef<Value*>( CS->getArgument(3) ), "", CS->getInstruction());
                            //CS->getInstruction()->removeFromParent();
                            replacedCallInstList.push_back(std::make_pair(newinst, CS));
                            //errs() << "after replace: " << BB << "\n";
                        }
                    }
                }
            }
        }

        for (auto pr : replacedCallInstList) {
            pr.second->getInstruction()->eraseFromParent();
        }

        errs() << "after preprocess\n";
        for (auto &func : M) {
            errs() << func.getName() << "::\n";
            errs() << func;
        }

        return true;
    }

    // We don't modify the program, so we preserve all analyses
    void ReplaceFunc::getAnalysisUsage(AnalysisUsage &AU) const {
    }

};



static RegisterPass<ACT::ReplaceFunc> X("ReplaceFunc", "ReplaceFunc Pass", false, true);
