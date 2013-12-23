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

    static std::vector<Function*> visitedFunc;
    void setupArguments(const CallSite *CS, PTGraph* graph) {
        Function *callee = CS->getCalledFunction();
        errs() << "setupArgument for func:" << callee->getName() << "\n";
        errs() << "setupArgument graph:\n"; graph->print(errs());
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
        // errs() << "graphForCallSite from caller: " << caller->getName() << "\n";
        // merge all context for the caller
        std::vector<PTGraph*> mergeList;
        for (auto& c : c2g) {
            if (c.first && c.first->getCalledFunction() == caller)
                mergeList.push_back(c.second);
            if (caller->getName() == "main" && c.first == NULL)
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
        // drop cycle.
        if (std::find(visitedFunc.begin(), visitedFunc.end(), function) != visitedFunc.end())
            return NULL;
        visitedFunc.push_back(function);
        if (added) *added = false;
        if (function->empty()) return NULL;
        std::deque<BasicBlock*> workingList;
        std::vector<BasicBlock*> unvisited;
        for (auto& BB : *function) {
            unvisited.push_back(&BB);
        }

        workingList.push_back(&function->getEntryBlock());
        while (!workingList.empty()) {
            bool modified = false;
            BasicBlock* BB = workingList.front();
            unvisited.erase(std::remove(unvisited.begin(), unvisited.end(), BB), unvisited.end());
            workingList.pop_front();
            //errs() << "working on BB: " << *BB << "\n";
            for (auto& inst : *BB) {
                bool newmod = runInstruction(flowinto, inst);
                modified |= newmod;
                if (newmod) {
                    errs() << "runInst modified " << inst << "\n";
                }
            }
            TerminatorInst* tinst = BB->getTerminator();
            for (unsigned i = 0; i < tinst->getNumSuccessors(); i++) {
                BasicBlock* succBB = tinst->getSuccessor(i);
                if (modified)
                    if (added) *added = true;
                if (modified || std::find(unvisited.begin(), unvisited.end(), succBB) != unvisited.end()) {
                    workingList.push_back(succBB);
                }
            }
        }
        for (auto &BB : *function) {
            for (auto &inst : BB) {
                if (isa<ReturnInst>(&inst)) {
                    ReturnInst* retInst = cast<ReturnInst>(&inst);
                    Value *v = retInst->getReturnValue();
                    if (v && v->getType()->isPointerTy()) {
                        // errs() << "returning value" << *v << "\n";
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
            if (added)
                errs() << "loadinst added after findOrCreateValue for v\n";
            Value *op = loadinst->getOperand(0);
            //errs() << "operands for load:" << loadinst << "\n";
            //errs() << *op << "\n";
            PTNode* loadop = graph->findOrCreateValue(op, false, &added);
            modified |= added;
            if (added)
                errs() << "loadinst added after findOrCreateValue for op\n";
            for (auto nodep : loadop->next) {
                for (auto nodepp : nodep->next) {
                    bool newmod = graph->addEdge(v, nodepp);
                    modified |= newmod;
                    if (newmod) {
                        errs() << "modified on addedge for two nodes: \n";
                        v->print(errs());
                        nodepp->print(errs());
                    }
                }
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
            errs() << "handling call: " << CS.getCalledFunction()->getName() << "\n";
            //graph->print(errs());
            //errs() << "^^^^^^^^^^^\n";
            // add each arguments in case
            for (unsigned i = 0; i < CS.arg_size(); i++) {
                //errs() << "creating node for" << *CS.getArgument(i) << "\n";
                modified |= graph->addNode(CS.getArgument(i)) != NULL;
            }
            errs() << "modified after addNode: " << modified << "\n";
            Function* callee = CS.getCalledFunction();
            if (callee->getName() == "pthread_mutex_init" ||
                callee->getName() == "pthread_mutex_lock" ||
                callee->getName() == "pthread_mutex_unlock")
                return false;
            // clone a graph to subprocess analyze
            PTGraph *newgraph = graph->clone();
            std::vector<PTNode*> trackingList(newgraph->nodes);
            errs() << "newgraph before setup and analyze::\n";
            newgraph->print(errs());
            // prepare the graph for callee.
            setupArguments(&CS, newgraph);
            PTNode* retNode = analyze(callee, newgraph, &added);
            //modified |= added;
            //errs() << "modified after analyze: " << modified << "\n";
            PTNode* vnode = newgraph->findOrCreateValue(&inst, false, &added);
            modified |= added;
            if (inst.getType()->isPointerTy() && retNode)
                for (auto retNodeadj : retNode->next)
                    modified |= newgraph->addEdge(vnode, retNodeadj);
            if (modified)
                errs() << "modified after retnode\n";
            if (std::find(trackingList.begin(), trackingList.end(), vnode) == trackingList.end())
                trackingList.push_back(vnode);

            newgraph->onlyTracking(trackingList);
            bool identical = newgraph->identicalTo(graph);
            modified |= !identical;
            errs() << "identical: " << identical << "\n";
            errs() << "modified: " << modified << "\n";
            if (modified)
                graph->merge(*newgraph);
            errs() << "graph after analyze call\n";
            graph->print(errs());
            newgraph->clear();
            delete newgraph;
            return modified;
        }

        if (isa<ReturnInst>(&inst)) {
            ReturnInst* retInst = cast<ReturnInst>(&inst);
            Value *v = retInst->getReturnValue();
            if (!v) return false;
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
    void FlowtoAnalysis::cleanupForFunc(Function *funcp, PTGraph *flowinto) {
        std::vector<PTNode*> track;
        for (auto nodep : flowinto->nodes) {
            if (isa<Instruction>(nodep->getValue())) {
                Instruction* inst = cast<Instruction>(nodep->getValue());
                if (inst->getParent()->getParent() != funcp)
                    continue;
            } else if (isa<Argument>(nodep->getValue())) {
                Argument *arg = cast<Argument>(nodep->getValue());
                if (arg->getParent() != funcp)
                    continue;
            }
            track.push_back(nodep);
        }
        flowinto->onlyTracking(track);
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
        while (!working_list.empty()) {
            auto funcp = working_list.back();
            working_list.pop_back();
            if (funcp->empty()) continue;
            errs() << "Working on: " << funcp->getName() << "\n";
            // find all its callsites
            for (auto worked_funcp : worked_list) {
                auto cgnodep = CG[worked_funcp];
                for (auto csrecord : *cgnodep) {
                    if (csrecord.second->getFunction() == funcp) {
                        auto *CS = new CallSite(csrecord.first);
                        assert(!!(*CS) && "This should be a callsite");
                        errs() << "called by callsite: " << csrecord.first << "from function:" << CS->getCaller()->getName() << "\n";
                        PTGraph* flowinto = graphForCallSite(*CS);
                        visitedFunc.clear();
                        analyze(funcp, flowinto);
                        c2g.insert(std::make_pair(CS, flowinto));
                        errs() << "called by callsite: " << csrecord.first << "from function:" << CS->getCaller()->getName() << "\n";
                        errs() << "result graph:\n";
                        flowinto->print(errs());
                    }
                }
            }
            if (funcp->getName() == "main") {
                PTGraph* flowinto = new PTGraph();
                visitedFunc.clear();
                analyze(funcp, flowinto);
                cleanupForFunc(funcp, flowinto);
                c2g.insert(std::make_pair((CallSite*)NULL, flowinto));
                flowinto->print(errs());
            }
            worked_list.push_back(funcp);
            auto cgnodep = CG[funcp];
            for (auto csrecord : *cgnodep) {
                CallSite CS(csrecord.first);
                Function *callee = CS.getCalledFunction();
                if (std::find(worked_list.begin(), worked_list.end(), callee) == worked_list.end() &&
                    std::find(working_list.begin(), working_list.end(), callee) == working_list.end() &&
                    !callee->empty())
                    working_list.push_back(callee);
            }
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
