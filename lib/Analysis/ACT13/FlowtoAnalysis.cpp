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


    void setupArguments(const CallSite *CS, PTGraph* graph) {
        Function *callee = CS->getCalledFunction();
        errs() << "setupArgument for func:" << callee->getName() << "\n";
        for (auto& arg : callee->getArgumentList()) {
            if (arg.getType()->isPointerTy()) {
                // need to setup the graph for it.
                errs() << "argnode value: " << arg << ", oldnode value: " << *CS->getArgument(arg.getArgNo()) << "\n";
                PTNode* argnode = graph->addNode(&arg);
                PTNode* oldnode = graph->findValue(CS->getArgument(arg.getArgNo()));
                assert(argnode);
                assert(oldnode);
                for (auto node : oldnode->next) {
                    graph->addEdge(argnode, node);
                }
            }
        }
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
        setupArguments(&CS, result);
        std::vector<PTNode*> trackingList;
        for (auto& arg : callee->getArgumentList()) {
            if (arg.getType()->isPointerTy()) {
                // need to setup the graph for it.
                PTNode* argnode = result->findValue(&arg);
                assert(argnode && "argnode shoule have been created");
                trackingList.push_back(argnode);
            }
        }
        for (auto nodep : result->nodes) {
            if (isa<GlobalValue>(nodep->getValue()))
                trackingList.push_back(nodep);
        }
        result->onlyTracking(trackingList);
        return result;
    }

    /**
     * Generate to PTGraph on the flowinto graph.
     * Return value: PTGraph node of the ret value.
     **/
    PTNode *FlowtoAnalysis::analyze(Function* function, PTGraph* flowinto, bool *added) {
        if (added) *added = false;
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
                if (added) *added = true;
                TerminatorInst* tinst = BB->getTerminator();
                for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                    workingList.push_back(tinst->getSuccessor(i));
                }
            }
        }
        for (auto &BB : *function) {
            for (auto &inst : BB) {
                if (isa<ReturnInst>(&inst)) {
                    ReturnInst* retInst = cast<ReturnInst>(&inst);
                    Value *v = retInst->getReturnValue();
                    if (v && v->getType()->isPointerTy()) {
                        errs() << "returning value" << *v << "\n";
                        PTNode* retNode = flowinto->findValue(v);
                        assert(retNode && "retNode should have been created");
                        return retNode;
                    }
                }
            }
        }
        return NULL;
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
        if (allocainst || (!!CS && CS.getCalledFunction()->getName() == "malloc")) {
            // just added, addNode will create the location node for us.
            graph->findOrCreateValue(&inst, false, &added);
            modified |= added;
            // PTNode *location = graph->addNode(&inst, true);

            // modified |= (location != NULL);
            // if (!location)
            //     location = graph->findValue(&inst, true);
            // modified |= graph->addEdge(v, location);
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
            // add each arguments in case
            for (unsigned i = 0; i < CS.arg_size(); i++) {
                errs() << "creating node for" << *CS.getArgument(i) << "\n";
                modified |= graph->addNode(CS.getArgument(i)) != NULL;
            }
            std::vector<PTNode*> trackingList(graph->nodes);
            // prepare the graph for callee.
            setupArguments(&CS, graph);
            PTNode* retNode = analyze(callee, graph, &added);
            modified |= added;
            PTNode* vnode = graph->findOrCreateValue(&inst, false, &added);
            modified |= added;
            if (inst.getType()->isPointerTy() && retNode)
                for (auto retNodeadj : retNode->next)
                    modified |= graph->addEdge(vnode, retNodeadj);
            if (std::find(trackingList.begin(), trackingList.end(), vnode) == trackingList.end())
                trackingList.push_back(vnode);
            graph->onlyTracking(trackingList);
            return modified;
        }

        if (isa<ReturnInst>(&inst)) {
            ReturnInst* retInst = cast<ReturnInst>(&inst);
            Value *v = retInst->getReturnValue();
            PTNode *vnode = graph->addNode(v);
            return vnode != NULL;
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
                            analyze(funcp, flowinto);
                            c2g.insert(std::make_pair(CS, flowinto));
                        }
                    }
                }
                if (funcp->getName() == "main") {
                    PTGraph* flowinto = new PTGraph();
                    analyze(funcp, flowinto);
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
