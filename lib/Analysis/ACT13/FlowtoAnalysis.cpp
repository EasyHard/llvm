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
#include "llvm/ACT13/ReplaceFunc.h"

using namespace llvm;

namespace ACT {
    char FlowtoAnalysis::ID = 0;

    FlowtoAnalysis::FlowtoAnalysis() : ModulePass(ID) {}

    void FlowtoAnalysis::releaseMemory() {
    }

    static std::vector<Function*> visitedFunc;
    void setupArguments(const CallSite *CS, PTGraph* graph) {
        Function *callee = CS->getCalledFunction();
        // errs() << "setupArgument for func:" << callee->getName() << "\n";
        // errs() << "setupArgument graph:\n"; graph->print(errs());
        for (auto& arg : callee->getArgumentList()) {
            if (arg.getType()->isPointerTy()) {
                // need to setup the graph for it.
                errs() << "argnode value: " << arg << ", oldnode value: " << *CS->getArgument(arg.getArgNo()) << "\n";
                bool added;
                PTNode* argnode = graph->findOrCreateValue(&arg, false, &added);
                PTNode* oldnode = graph->findValue(CS->getArgument(arg.getArgNo()));
                assert(argnode);
                assert(oldnode);
                for (auto node : oldnode->next) {
                    graph->addEdge(argnode, node);
                }
            }
        }

        std::vector<PTNode*> trackingList;
        for (auto& arg : callee->getArgumentList()) {
            if (arg.getType()->isPointerTy()) {
                // need to setup the graph for it.
                PTNode* argnode = graph->findValue(&arg);
                assert(argnode && "argnode shoule have been created");
                trackingList.push_back(argnode);
            }
        }
        for (auto nodep : graph->nodes) {
            if (isa<GlobalValue>(nodep->getValue()))
                trackingList.push_back(nodep);
        }
        graph->onlyTracking(trackingList);
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

    // Take callsite and graph as input, generate new graph in csResult
    // and update csInput for called function.
    bool FlowtoAnalysis::analyze(CallSite *csp, PTGraph* flowinto) {
        Function *function;
        if (csp == NULL) function = mainp;
        else function = csp->getCalledFunction();
        if (function->empty()) return false;
        PTGraph *graph = flowinto->clone();

        bool modified = true;
        while (modified) {
            modified = false;
            for (auto& BB : *function) {
                for (auto& inst : BB) {
                    bool newmod = runInstruction(graph, inst);
                    modified |= newmod;
                }
            }
        }
        PTNode* retNode = NULL;
        for (auto &BB : *function) {
            for (auto &inst : BB) {
                if (isa<ReturnInst>(&inst)) {
                    ReturnInst* retInst = cast<ReturnInst>(&inst);
                    Value *v = retInst->getReturnValue();
                    if (v && v->getType()->isPointerTy()) {
                        // errs() << "returning value" << *v << "\n";
                        retNode = graph->findValue(v);
                        assert(retNode && "retNode should have been created");
                    }
                }
            }
        }

        auto resultPair = csResult[csp];
        if (resultPair.first == NULL) {
            csResult[csp] = std::make_pair(graph, retNode);
            return true;
        } else {
            PTGraph *resultGraph = resultPair.first;
            if (graph->identicalTo(resultGraph)) {
                graph->clear();
                delete graph;
                return false;
            }
            else {
                resultGraph->clear();
                delete resultGraph;
                csResult[csp] = std::make_pair(graph, retNode);
                return true;
            }
        }
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
            // if (added)
            //     errs() << "loadinst added after findOrCreateValue for v\n";
            Value *op = loadinst->getOperand(0);
            //errs() << "operands for load:" << loadinst << "\n";
            //errs() << *op << "\n";
            PTNode* loadop = graph->findOrCreateValue(op, false, &added);
            modified |= added;
            // if (added)
            //     errs() << "loadinst added after findOrCreateValue for op\n";
            for (auto nodep : loadop->next) {
                for (auto nodepp : nodep->next) {
                    bool newmod = graph->addEdge(v, nodepp);
                    modified |= newmod;
                    // if (newmod) {
                    //     errs() << "modified on addedge for two nodes: \n";
                    //     v->print(errs());
                    //     nodepp->print(errs());
                    // }
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
            // errs() << "modified after addNode: " << modified << "\n";
            Function* callee = CS.getCalledFunction();
            if (callee->getName() == "pthread_mutex_init" ||
                callee->getName() == "pthread_mutex_lock" ||
                callee->getName() == "pthread_mutex_unlock")
                return false;

            // call to somewhere else, modified its csInput.
            CallSite* csip = NULL;
            if (callee != mainp) {
                for (auto pr : csInput) {
                    if (pr.first && pr.first->getInstruction() == CS.getInstruction())
                        csip = pr.first;
                }
                assert(csip && "csInput should be inited");
            }

            PTGraph* csiGraph = csInput[csip];
            PTGraph* tryMerge = csiGraph->clone();
            tryMerge->merge(*graph);
            setupArguments(csip, tryMerge);
            if (!tryMerge->identicalTo(csiGraph)) {
                errs() << "csiGraph is not identical to tryMerge\n";
                modified = true;
                csiGraph->merge(*tryMerge);
                errs() << "csiGraph nodes: " << csiGraph->nodes.size() << "\n";
            }
            tryMerge->clear();
            delete tryMerge;
            errs() << "csInput[csip]->nodes.size() == " << csInput[csip]->nodes.size() << "\n";

            // fetch its result as summary
            PTGraph *newgraph = graph->clone();
            auto pr = csResult[csip];
            if (pr.first == NULL)
                return false;
            PTGraph *resultGraph = pr.first;
            PTNode *retNode_f = pr.second;
            std::vector<PTNode*> trackingList(newgraph->nodes);
            newgraph->merge(*resultGraph);
            PTNode* vnode = newgraph->findOrCreateValue(&inst, false, &added);
            PTNode *retNode = NULL;
            if (retNode_f)
                retNode = newgraph->findOrCreateValue(retNode_f->getValue(), retNode_f->isLocation(), &added);
            if (inst.getType()->isPointerTy() && retNode)
                for (auto retNodeadj : retNode->next)
                    modified |= newgraph->addEdge(vnode, retNodeadj);

            newgraph->onlyTracking(trackingList);
            bool identical = newgraph->identicalTo(graph);
            modified |= !identical;
            // errs() << "identical: " << identical << "\n";
            // errs() << "modified: " << modified << "\n";
            if (modified)
                graph->merge(*newgraph);
            // errs() << "graph after analyze call\n";
            // graph->print(errs());
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

    /**
     * Cleanup the untrack nodes from PTGraph
     **/
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
        errs() << "CG print:::::\n";
        CG.print(errs(), &M);
        // init
        for (auto pr : CG) {
            if (pr.first == NULL) continue;
            if (pr.first->empty()) continue;
            for (auto csrecord : *pr.second) {
                if (csrecord.second->getFunction()->empty()) continue;
                CallSite* CS = new CallSite(csrecord.first);
                PTGraph* emptyGraph = new PTGraph();
                csInput.insert(std::make_pair(CS, emptyGraph));
            }
        }

        // init for main
        CallSite* CS = NULL; PTGraph* emptyGraph = new PTGraph();
        csInput.insert(std::make_pair(CS, emptyGraph));
        for (auto& func : M) if (func.getName() == "main") mainp = &func;
        assert(mainp && "module should have a \"main\" as entry point");

        bool modified = true;
        while (modified) {
            modified = false;
            for (auto pr: csInput) {
                if (pr.first)
                    errs() << "Working on " << pr.first->getCalledFunction()->getName() << "\n";
                else
                    errs() << "Working on main\n";
                modified |= analyze(pr.first, pr.second);

                // debugging
                errs() << "csInputMiddleResult::\n";
                for (auto pr: csInput) {
                    if (pr.first == NULL) continue;
                    errs() << "call to " << pr.first->getCalledFunction()->getName() << " from " << pr.first->getInstruction()->getParent()->getParent()->getName() << "\n";
                    pr.second->print(errs());
                    errs() << "@@@@@@@@@@@@@\n";
                }

            }
        }
        // Settled down, generate `c2g` to keep interface.
        for (auto pr : csResult)
            c2g.insert(std::make_pair(pr.first, pr.second.first));

        // debugging
        errs() << "csInputLastResult::\n";
        for (auto pr: csInput) {
            if (pr.first == NULL) continue;
            errs() << "call to " << pr.first->getCalledFunction()->getName() << " from " << pr.first->getInstruction()->getParent()->getParent()->getName() << "\n";
            pr.second->print(errs());
            errs() << "@@@@@@@@@@@@@\n";
        }

        // create a context-insensitive result for each function
        for (auto& func : M) {
            if (func.empty()) continue;
            PTGraph *graph = new PTGraph();
            for (auto pr : c2g) {
                CallSite* CS = pr.first;
                PTGraph *CSGraph = pr.second;
                if ((CS && CS->getCalledFunction() == &func) || (CS == NULL && func.getName() == "main")) {
                    graph->merge(*CSGraph);
                }
            }
            f2g.insert(std::make_pair(&func, graph));
            errs() << "Merge Graph for function " << func.getName() << "\n";
            graph->print(errs());
        }
        return false;
    }

    // We don't modify the program, so we preserve all analyses
    void FlowtoAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<ReplaceFunc>();
        AU.addRequired<CallGraph>();
        AU.setPreservesAll();
    }

};



static RegisterPass<ACT::FlowtoAnalysis> X("FlowtoAnalysis", "FlowtoAnalysis Pass", false, false);
