 #include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <list>

#include "llvm/ACT13/RaceDetector.h"
#include "llvm/ACT13/ShareAnalysis.h"
#include "llvm/ACT13/LockDomAnalysis.h"
#include "llvm/ACT13/FlowtoAnalysis.h"
#include "llvm/ACT13/ReplaceFunc.h"

using namespace llvm;

namespace ACT {

    static bool isUset(const std::set<Value *>& x) {
        return x.size() == 1 && *x.begin() == NULL;
    }

    static std::set<Value*> intersection(const std::set<Value *>&a, const std::set<Value*>&b) {
        if (isUset(a))
            return b;
        if (isUset(b))
            return a;
        std::set<Value *>result;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
            std::insert_iterator<decltype(result)>(result, result.begin()));
        return result;
    }

    char RaceDetector::ID = 0;

    RaceDetector::RaceDetector() : ModulePass(ID) {}

    void RaceDetector::releaseMemory() {
    }

    bool RaceDetector::doFinalization(Module &M) {

        return true;
    }

    std::vector<CallSite*> RaceDetector::getCallSiteList(Function* funcp, Module& M) {
        std::vector<CallSite*> result;
        if (funcp->getName() == "main") result.push_back((CallSite*)NULL);
        FlowtoAnalysis& ft = getAnalysis<FlowtoAnalysis>();
        for (auto pr : ft.c2g) {
            if (pr.first == NULL)
                continue;
            if (pr.first->getCalledFunction() == funcp)
                result.push_back(pr.first);
        }
        return result;
    }

    bool RaceDetector::runOnModule(Module &M) {
        ShareAnalysis& sa = getAnalysis<ShareAnalysis>();
        LockDomAnalysis& lda = getAnalysis<LockDomAnalysis>();
        FlowtoAnalysis& fta = getAnalysis<FlowtoAnalysis>();
        for (auto pr : sa.sharingLocation) {
            Value *placep = pr.first;
            auto instList = pr.second;
            std::set<Value*> lockset; lockset.insert(NULL); // universal set
            for (auto instp : instList) {
                Function* funcp = instp->getParent()->getParent();
                std::vector<CallSite*> csList = getCallSiteList(funcp, M);
                for (auto csp : csList) {

                    // if (csp != NULL) {
                    //     errs() << "[RD] CallSite from " << csp->getInstruction()->getParent()->getParent()->getName();
                    //     errs() << " to " << funcp->getName() << "\n";
                    // } else {
                    //     errs() << "[RD] Entry point of main\n";
                    // }

                    // first check if the current context will access the place
                    Value *pp = NULL;
                    if (isa<LoadInst>(instp)) {
                        LoadInst* loadp = cast<LoadInst>(instp);
                        pp = loadp->getPointerOperand();
                    } else {
                        StoreInst* storep = cast<StoreInst>(instp);
                        pp = storep->getPointerOperand();
                    }
                    PTNode* node = fta.c2g[csp]->findValue(pp);
                    // skip this context if the instruction will not access the place
                    if (std::find_if(node->next.begin(), node->next.end(), [&](const PTNode* x)->bool {
                                return x->getValue() == placep && x->isLocation();
                            }) == node->next.end())
                        continue;
                    auto idom = lda.analyzeCallSite(csp, M);
                    auto it = idom.find(instp);
                    if (it == idom.end()) {
                        errs() << "[RD] inst: " << *instp << "\nidom:\n";
                        for (auto pr: idom) {
                            errs() << *pr.first << "\n";
                        }
                    }
                    assert(it != idom.end() && "inst should be found in idom map");
                    int oldsize = lockset.size();
                    lockset = intersection(lockset, it->second);
                    if (oldsize != 0 && lockset.size() == 0) {
                        errs() << "[RD] oops, please check " << *instp << "in func ";
                        errs() << instp->getParent()->getParent()->getName() << "\n";
                    }
                }
            }
            if (lockset.size() == 0) {
                errs() << "[RD] value " << *placep << " may be accessed without proper lock\n";
                errs() << "[RD] Check for the following instruciton: \n";
                for (auto instp : instList)
                    errs() << "[RD]" << *instp << "in func " << instp->getParent()->getParent()->getName() << "\n";
                errs() << "\n";
            }
        }
        return false;
    }

    // We don't modify the program, so we preserve all analyses
    void RaceDetector::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<FlowtoAnalysis>();
        AU.addRequired<ShareAnalysis>();
        AU.addRequired<LockDomAnalysis>();
        AU.setPreservesAll();
    }

};



static RegisterPass<ACT::RaceDetector> X("RaceDetector", "RaceDetector Pass", false, false);
