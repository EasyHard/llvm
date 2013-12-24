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

#include "llvm/ACT13/ShareAnalysis.h"
#include "llvm/ACT13/FlowtoAnalysis.h"
#include "llvm/ACT13/ReplaceFunc.h"

using namespace llvm;

namespace ACT {
    char ShareAnalysis::ID = 0;

    ShareAnalysis::ShareAnalysis() : ModulePass(ID) {}

    void ShareAnalysis::releaseMemory() {
    }

    template<typename T>
    void push_back_ifnot(std::list<T>& v, T t) {
        if (std::find(v.begin(), v.end(), t) == v.end())
            v.push_back(t);
    }

    std::set<PTNode*> ShareAnalysis::accessingLocation(std::vector<Instruction*>& list) {
        std::set<PTNode*> set;
        Value *v = NULL;
        for (auto inst :list) {
            assert(isa<LoadInst>(inst) || isa<StoreInst>(inst));
            if (isa<LoadInst>(inst)) {
                LoadInst *loadInst = cast<LoadInst>(inst);
                v = loadInst->getPointerOperand();
            } else {
                StoreInst *storeInst = cast<StoreInst>(inst);
                v = storeInst->getPointerOperand();
            }
            FlowtoAnalysis& ftanalysis = getAnalysis<FlowtoAnalysis>();
            Function *funcp = inst->getParent()->getParent();
            auto it = ftanalysis.f2g.find(funcp);
            assert(it != ftanalysis.f2g.end() && "flowto graph should be calced");
            PTGraph *graph = it->second;
            PTNode *node = graph->findValue(v);
            assert(node && "should find node for value");
            for (auto nodep: node->next) {
                if (nodep->isLocation())
                    set.insert(nodep);
            }
        }
        return set;
    }

    void ShareAnalysis::handleInst(std::vector<Instruction*>& accessList, std::list<BasicBlock*>&blockList, Instruction &inst) {
        CallSite CS(&inst);
        if (!!CS) {
            if (CS.getCalledFunction()->empty()) return;
            push_back_ifnot(blockList, &CS.getCalledFunction()->getEntryBlock());
        } else {
            if (isa<LoadInst>(&inst) || isa<StoreInst>(&inst)) {
                accessList.push_back(&inst);
            } else if (isa<TerminatorInst>(&inst)) {
                TerminatorInst *tinst = cast<TerminatorInst>(&inst);
                for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                    push_back_ifnot(blockList, tinst->getSuccessor(i));
                }
            }
        }
    }

    std::vector<Instruction*> ShareAnalysis::accessFrom(BasicBlock *BB) {
        std::vector<Instruction*> accessList;
        std::list<BasicBlock *> blockList;
        blockList.push_back(BB);
        auto curr = blockList.begin();

        while (curr != blockList.end()) {
            BasicBlock* currBB = *curr;
            for (auto& inst : *currBB) {
                handleInst(accessList, blockList, inst);
            }
            curr++;
        }
        return accessList;
    }
    std::vector<Instruction*> ShareAnalysis::accessFrom(Instruction *inst) {
        std::vector<Instruction*> accessList;
        std::list<BasicBlock *> blockList;
        BasicBlock *firstBB = inst->getParent();
        auto it = firstBB->begin(), be = firstBB->end();
        while (it != be && &(*it) != inst) {
            //errs() << it << ", " << be << "\n";
            it++;
        }
        assert(it != be && "inst shoudl be found in BB");

        while (it != be) {
            handleInst(accessList, blockList, *it);
            ++it;
        }
        auto curr = blockList.begin();
        while (curr != blockList.end()) {
            BasicBlock* currBB = *curr;
            //errs() << "handling BB size: " << currBB->size();
            for (auto& inst : *currBB) {
                handleInst(accessList, blockList, inst);
            }
            curr++;
        }
        return accessList;
    }

    bool ShareAnalysis::doFinalization(Module &M) {
        return true;
    }

    bool ShareAnalysis::runOnModule(Module &M) {
        ReplaceFunc& rf = getAnalysis<ReplaceFunc>();
        // for each thread entry point, analysis its access
        // and access after it, to address shared location
        for (auto &pr : rf.replacedCallInstList) {
            Function *funcp = pr.first->getCalledFunction();
            errs() << "[ShareAnalysis] working on " << funcp->getName() << "\n";
            std::vector<Instruction*> threadAccess = accessFrom(&funcp->getEntryBlock());
            Instruction *inst = pr.first;
            BasicBlock* BB = inst->getParent();
            auto it = BB->begin();
            while (it != BB->end() && &(*it) != inst) it++;
            assert(it != BB->end() && "inst should be found");
            it++; // move to the next
            assert(it != BB->end());
            errs() << "analyzing afterAccess from " << *it << "\n";
            std::vector<Instruction*> afterAccess = accessFrom(&(*it));
            errs() << "[SA] threadAccess Inst:\n";
            for (auto instp : threadAccess) {
                errs() << *instp << "\n";
            }
            errs() << "[SA] afterAccess Inst:\n";
            for (auto instp : afterAccess) {
                errs() << *instp << "\n";
            }
            std::set<PTNode*> threadAccessSet = accessingLocation(threadAccess);
            std::set<PTNode*> afterAccessSet = accessingLocation(afterAccess);
            errs() << "[SA] threadAccessSet Node:\n";
            for (auto nodep : threadAccessSet) {
                nodep->print(errs());
            }
            errs() << "[SA] afterAccessSet Node:\n";
            for (auto nodep : afterAccessSet) {
                nodep->print(errs());
            }
            // intersection for sharing location, Value for memory place holder
            std::vector<Value*> sharingList;
            for (auto nodep1: threadAccessSet) {
                for (auto nodep2 : afterAccessSet) {
                    assert(nodep1->isLocation() && nodep2->isLocation());
                    if (nodep1->getValue() == nodep2->getValue())
                        sharingList.push_back(nodep1->getValue());
                }
            }
            sharingList.erase(std::unique(sharingList.begin(), sharingList.end()), sharingList.end());
            errs() << sharingList.size() << " sharing location(s) for thread: " << funcp->getName() << "\n";
            for (auto valuep : sharingList) {
                errs() << *valuep << "\n";
            }
            // merge the two instruction set
            threadAccess.insert(threadAccess.end(), afterAccess.begin(), afterAccess.end());
            std::sort(threadAccess.begin(), threadAccess.end());
            errs() << "before unique, num of inst: " << threadAccess.size() << "\n";
            threadAccess.erase(std::unique(threadAccess.begin(), threadAccess.end()), threadAccess.end());
            errs() << "after unique, num of inst: " << threadAccess.size() << "\n";
            for (auto instp : threadAccess) {
                std::vector<Instruction*> v;v.push_back(instp);
                std::set<PTNode*> accSet = accessingLocation(v);
                for (auto nodep : accSet) {
                    if (std::find(sharingList.begin(), sharingList.end(), nodep->getValue()) != sharingList.end()) {
                        errs() << "Instruction: " << *instp << " in " << instp->getParent()->getParent()->getName() << "\n";
                        sharingLocation[nodep->getValue()].push_back(instp);
                    }
                }
            }
            errs() << "-------\n";
        }
        return false;
    }

    // We don't modify the program, so we preserve all analyses
    void ShareAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<FlowtoAnalysis>();
        AU.addRequired<ReplaceFunc>();
        AU.setPreservesAll();
    }

};



static RegisterPass<ACT::ShareAnalysis> X("ShareAnalysis", "ShareAnalysis Pass", false, false);
