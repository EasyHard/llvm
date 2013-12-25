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

#include "llvm/ACT13/LockDomAnalysis.h"
#include "llvm/ACT13/FlowtoAnalysis.h"
#include "llvm/ACT13/ReplaceFunc.h"

using namespace llvm;

namespace ACT {
    char LockDomAnalysis::ID = 0;

    LockDomAnalysis::LockDomAnalysis() : ModulePass(ID) {}

    void LockDomAnalysis::releaseMemory() {
    }

    bool LockDomAnalysis::doFinalization(Module &M) {

        return true;
    }

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


    std::map<Instruction*, std::set<Value*>> LockDomAnalysis::analyzeCallSite(CallSite* CS, Module& M) {
        if (CS && CS->getCalledFunction()->empty())
            return std::map<Instruction*, std::set<Value*>>(); // return empty set.
        std::set<Value*> head;
        Function* funcp;
        if (CS == NULL) {
            for (auto& func : M)
                if (func.getName() == "main")
                    funcp = &func;
        } else {
            Instruction* callInst = CS->getInstruction();
            funcp = CS->getCalledFunction();
            auto it = dom.find(callInst);
            assert(it != dom.end() && "Callsite should be analyzed");
            head = it->second;
        }
        // locks that must be hold at the entry of block
        std::map<BasicBlock*, std::set<Value*>> bdom;
        // symbolic for a universal set
        std::set<Value *> uset;
        uset.insert(NULL);
        // init
        for (auto &BB : *funcp) {
            bdom[&BB] = uset;
        }
        bdom[&funcp->getEntryBlock()] = head;
        std::map<Instruction*, std::set<Value*>> dom;
        FlowtoAnalysis& ft = getAnalysis<FlowtoAnalysis>();
        PTGraph *graph = ft.c2g[CS];
        bool modified = true;
        while (modified) {
            modified = false;
            for (auto &BB : *funcp) {
                std::set<Value*> currset = bdom[&BB];
                for (auto &inst : BB) {
                    dom[&inst] = currset;
                    CallSite CS(&inst);
                    if (!!CS) {
                        if (CS.getCalledFunction()->getName() == "pthread_mutex_unlock") {
                            Value *v = CS.getArgument(0);
                            PTNode* node = graph->findValue(v);
                            assert(node && "should have node here");
                            for (auto nodep : node->next) {
                                if (nodep->isLocation())
                                    currset.erase(nodep->getValue());
                            }
                        } else if (CS.getCalledFunction()->getName() == "pthread_mutex_lock") {
                            Value *v = CS.getArgument(0);
                            PTNode* node = graph->findValue(v);
                            // errs() << "locking value in func " << funcp->getName() << ": " << *v << "\n";
                            // errs() << "with node::";
                            // node->print(errs());
                            assert(node && "should have node here");
                            if (node->next.size() == 1) {
                                assert(node->next[0]->isLocation() && "the only lock should be a location");
                                currset.insert(node->next[0]->getValue());
                            }
                        } else {
                            // a call to somewhere
                            // TODO: Just leave it for now. Assuming every taken locks will be unlock within the function.
                            }
                        }
                    if (isa<TerminatorInst>(&inst)) {
                        TerminatorInst* tinst = cast<TerminatorInst>(&inst);
                        for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                            BasicBlock *BB = tinst->getSuccessor(i);
                            std::set<Value*> newset = intersection(bdom[BB], currset);
                            if (newset != bdom[BB]) {
                                modified = true;
                                bdom[BB] = newset;
                            }
                        }
                    }
                }
            }
        }

        return dom;
    }

    bool LockDomAnalysis::runOnModule(Module &M) {
        // locks that must be hold at the entry of function
        std::map<Function*, std::set<Value*>> fdom;
        // locks that must be hold at the entry of block
        std::map<BasicBlock*, std::set<Value*>> bdom;
        // symbolic for a universal set
        std::set<Value *> uset;
        uset.insert(NULL);
        ReplaceFunc &rf = getAnalysis<ReplaceFunc>();
        std::vector<Function*> threadEntryList;
        for (auto &pr : rf.replacedCallInstList) {
            threadEntryList.push_back(pr.first->getCalledFunction());
        }
        // init
        for (auto &func : M) {
            if (func.empty()) continue;
            if (func.getName() == "main")
                // empty set
                fdom[&func] = std::set<Value*>();
            else
                fdom[&func] = uset;
            if (std::find(threadEntryList.begin(), threadEntryList.end(), &func) != threadEntryList.end())
                fdom[&func] = std::set<Value*>();
            for (auto &BB : func) {
                bdom[&BB] = uset;
            }
        }
        FlowtoAnalysis& ft = getAnalysis<FlowtoAnalysis>();
        bool modified = true;
        while (modified) {
            modified = false;
            for (auto &func : M) {
                if (func.empty()) continue;
                bdom[&func.getEntryBlock()] = fdom[&func];
                if (func.getName() == "work") {
                    errs() << "func " << func.getName() << " fdom:\n";
                    for (auto vp : fdom[&func]) {
                        errs() << *vp << "\n";
                    }
                    errs() << "----end of fdom----\n";
                }
                for (auto &BB : func) {
                    std::set<Value*> currset = bdom[&BB];
                    for (auto &inst : BB) {
                        dom[&inst] = currset;
                        CallSite CS(&inst);
                        if (!!CS) {
                            if (CS.getCalledFunction()->getName() == "pthread_mutex_unlock") {
                                Value *v = CS.getArgument(0);
                                PTNode* node = ft.f2g[&func]->findValue(v);
                                assert(node && "should have node here");
                                for (auto nodep : node->next) {
                                    if (nodep->isLocation())
                                        currset.erase(nodep->getValue());
                                }
                            } else if (CS.getCalledFunction()->getName() == "pthread_mutex_lock") {
                                Value *v = CS.getArgument(0);
                                // errs() << "locking value in func " << func.getName() << ": " << *v << "\n";
                                PTNode* node = ft.f2g[&func]->findValue(v);
                                assert(node && "should have node here");
                                if (node->next.size() == 1) {
                                    assert(node->next[0]->isLocation() && "the only lock should be a location");
                                    currset.insert(node->next[0]->getValue());
                                }
                            } else {
                                // a call to somewhere
                                if (CS.getCalledFunction()->empty()) continue;
                                // don't modify the dom flow of thread entry
                                if (std::find(threadEntryList.begin(), threadEntryList.end(), CS.getCalledFunction()) == threadEntryList.end()) {
                                    Function* funcp = CS.getCalledFunction();
                                    std::set<Value*> newset = intersection(fdom[funcp], currset);
                                    if (newset != fdom[funcp]) {
                                        modified = true;
                                        fdom[funcp] = newset;
                                    }
                                }
                            }
                        }
                        if (isa<TerminatorInst>(&inst)) {
                            TerminatorInst* tinst = cast<TerminatorInst>(&inst);
                            for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                                BasicBlock *BB = tinst->getSuccessor(i);
                                std::set<Value*> newset = intersection(bdom[BB], currset);
                                if (newset != bdom[BB]) {
                                    modified = true;
                                    bdom[BB] = newset;
                                }
                            }
                        }
                    }
                }
            }
        }
        for (auto pr : dom) {
            if (pr.second.size() != 0) {
                errs() << "[LDA] " << *pr.first << " in func " << pr.first->getParent()->getParent()->getName() << " hold locks\n";
                for (auto vp : pr.second)
                    errs() << *vp << "\n";
            }
        }
        return false;
    }

    // We don't modify the program, so we preserve all analyses
    void LockDomAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<FlowtoAnalysis>();
        AU.addRequired<ReplaceFunc>();
        AU.setPreservesAll();
    }

};



static RegisterPass<ACT::LockDomAnalysis> X("LockDomAnalysis", "LockDomAnalysis Pass", false, false);
