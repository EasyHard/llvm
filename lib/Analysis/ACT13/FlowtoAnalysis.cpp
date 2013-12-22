#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/Module.h"
#include <vector>
#include <deque>

#include "llvm/ACT13/FlowtoAnalysis.h"


using namespace llvm;

namespace ACT {
    char FlowtoAnalysis::ID = 0;

    FlowtoAnalysis::FlowtoAnalysis() : ModulePass(ID) {}

    void FlowtoAnalysis::releaseMemory() {
    }

    /**
     * Generate the flowinto graph for a Callsite.
     * For arguments of the function that the CallSite callinto.
     **/
    PTGraph* FlowtoAnalysis::graphForCallSite(const CallSite& CS) {
        Function* caller = CS.getCaller();
        // merge all context for the caller
        std::vector<PTGraph*> mergeList;
        for (auto& c : c2g) {
            if (c.first->getCalledFunction() == caller)
                mergeList.push_back(c.second);
        }
        PTGraph *result = new PTGraph();
        for (auto graphp : mergeList) {
            result->merge(*graphp);
        }
        // Function which is called.
        Function *callee = CS.getCalledFunction();
        // setup arguments for callee
        //setupArguments(&CS, result);
        std::vector<PTNode*> trackingList;
        for (auto& arg : callee->getArgumentList()) {
            if (arg.getType()->isPointerTy()) {
                // need to setup the graph for it.
                PTNode* argnode = result->addNode(&arg);
                PTNode* oldnode = result->findValue(CS.getArgument(arg.getArgNo()));
                for (auto node : oldnode->next) {
                    result->addEdge(argnode, node);
                }
                trackingList.push_back(argnode);
            }
        }
        result->onlyTracking(trackingList);
        return result;
    }

    /**
     * Generate to PTGraph on the flowinto graph
     **/
    PTGraph *FlowtoAnalysis::analyze(Function* function, PTGraph* flowinto) {
        std::deque<BasicBlock*> workingList;
        workingList.push_back(&function->getEntryBlock());
        while (!workingList.empty()) {
            bool modified = false;
            BasicBlock* BB = workingList.front();
            workingList.pop_front();
            for (auto& inst : *BB) {
                modified |= runInstruction(flowinto, inst);
            }
            if (modified) {
                TerminatorInst* tinst = BB->getTerminator();
                for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                    workingList.push_back(tinst->getSuccessor(i));
                }
            }
        }
        return flowinto;
    }

    /**
     * Modify the PTGraph to a running instruction
     * return true if graph is modified.
     **/
    bool FlowtoAnalysis::runInstruction(PTGraph* graph, Instruction& inst) {
        bool modified = false;
        bool added;
        AllocaInst* allocainst = dyn_cast<AllocaInst>(&inst);
        CallSite CS(&inst);
        if (allocainst || (CS && CS.getCalledFunction()->getName() == "malloc")) {
            PTNode *v = graph->findOrCreateValue(&inst, false, &added);
            modified |= added;
            PTNode *location = graph->addNode(&inst, true);

            modified |= (location != NULL);
            if (!location)
                location = graph->findValue(&inst, true);
            modified |= graph->addEdge(v, location);
            return modified;
        }

        LoadInst* loadinst = dyn_cast<LoadInst>(&inst);
        if (loadinst) {
            PTNode *v = graph->findOrCreateValue(&inst, false, &added);
            modified |= added;
            errs() << "operands for load:" << loadinst << "\n";
            Value *op = loadinst->getOperand(0);
            errs() << *op << "\n";
            PTNode* loadop = graph->findOrCreateValue(op, false, &added);
            modified |= added;
            for (auto nodep : loadop->next) {
                for (auto nodepp : nodep->next)
                    modified |= graph->addEdge(v, nodepp);
            }
            return modified;
        }

        if (isa<StoreInst>(&inst)) {
            StoreInst *storeinst = dyn_cast<StoreInst>(&inst);
            Value* v = storeinst->getValueOperand();
            Value* p = storeinst->getPointerOperand();
            if (!v->getType()->isPointerTy())
                return false;
            bool added;
            PTNode* vnode = graph->findOrCreateValue(v, false, &added);
            modified |= added;
            PTNode* pnode = graph->findOrCreateValue(p, false, &added);
            modified |= added;
            for (auto vnodeadj : vnode->next)
                for (auto pnodeadj : pnode->next) {
                    modified |= graph->addEdge(pnodeadj, vnodeadj);
                }
            return modified;
        }

        if (!!CS) {
            Function* callee = CS.getCalledFunction();
            if (callee->getName() == "pthread_mutex_init" ||
                callee->getName() == "pthread_mutex_lock" ||
                callee->getName() == "pthread_mutex_unlock")
                return false;
            std::vector<PTNode*> trackingList(graph->nodes);
            // prepare the graph for callee.
            
        }

        if (!inst.getType()->isPointerTy()) {
            return false;
        } else if (!CS) {
            PTNode *node = graph->findValue(&inst);
            if (node == NULL) {
                node = graph->addNode(&inst);
                assert(node && "node created");
                modified = true;
            }
            for (auto VI = inst.value_op_begin(), VE = inst.value_op_end(); VI != VE; VI++) {
                PTNode* opnode = graph->findValue((*VI));
                if (opnode == NULL) {
                    opnode = graph->addNode(*VI);
                    assert(opnode && "opnode created");
                    modified = true;
                }
                for (auto nodep : opnode->next)
                    modified |= graph->addEdge(node, nodep);

            }
            return modified;
        }
        return false;
    }

    bool FlowtoAnalysis::runOnModule(Module &M) {
        CallGraph &CG = getAnalysis<CallGraph>();
        std::vector<Function*> working_list, worked_list;
        for (auto& func : M) {
            auto args = func.getFunctionType();
            // check if there is pointer type
            bool hasPointer = false;
            for (int i = 0, nParam = args->getNumParams();
                 i < nParam; i++) {
                if (isa<PointerType>(args->getParamType(i)))
                    hasPointer = true;
            }
            if (!hasPointer) working_list.push_back(&func);
        }
        errs() << "first working list:\n";
        for (auto funcp : working_list) {
            errs() << funcp->getName() << ", ";
        }
        errs() << "\n";
        while (!working_list.empty()) {
            for (auto funcp : working_list) {
                // find all its callsites
                for (auto worked_funcp : worked_list) {
                    auto cgnodep = CG[worked_funcp];
                    for (auto csrecord : *cgnodep) {
                        if (csrecord.second->getFunction() == funcp) {
                            auto *CS = new CallSite(csrecord.first);
                            assert(CS && "This should be a callsite");
                            errs() << "called by callsite: " << csrecord.first << "from function:" << CS->getCaller()->getName();
                            PTGraph* flowinto = graphForCallSite(*CS);
                            PTGraph* result = analyze(funcp, flowinto);
                            c2g.insert(std::make_pair(CS, result));
                        }
                    }
                }
                if (funcp->getName() == "main") {
                    PTGraph* flowinto = new PTGraph();
                    flowinto = analyze(funcp, flowinto);
                    c2g.insert(std::make_pair((CallSite*)NULL, flowinto));
                    flowinto->print(errs());
                }
            }
            break;
        }
        // while (modified) {
        //     modified = false;
        //     for (auto& func : M) {
        //         errs() << "looking into: " << func.getName() << "\n";
        //     }
        // }
        return false;
    }

    // We don't modify the program, so we preserve all analyses
    void FlowtoAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<CallGraph>();
        AU.setPreservesAll();
    }

};



static RegisterPass<ACT::FlowtoAnalysis> X("FlowtoAnalysis", "FlowtoAnalysis Pass", false, false);
