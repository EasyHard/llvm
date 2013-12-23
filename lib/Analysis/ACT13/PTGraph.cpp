#include "llvm/ACT13/PTGraph.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CallSite.h"
#include <algorithm>
namespace ACT {
    void PTGraph::merge(PTGraph& from) {
        // first, creating missed nodes
        for (auto theirnode : from.nodes) {
            Value *v = theirnode->getValue();
            if (!findValue(v, theirnode->isLocation())) {
                addNode(v, theirnode->isLocation());
            }
        }
        // then we could add missing edges
        for (auto theirnode : from.nodes) {
            PTNode* mynode = findValue(theirnode->getValue(), theirnode->isLocation());
            assert(mynode && "all missed nodes should be created");
            for (auto neiberhood : theirnode->next) {
                addEdge(mynode, findValue(neiberhood->getValue(), theirnode->isLocation()));
            }
        }
    }

    PTNode* PTGraph::findValue(Value *v, bool isLocation) {
        auto it = std::find_if(nodes.begin(), nodes.end(),
            [v, isLocation](const PTNode* x)->bool {return x->getValue() == v && x->isLocation() == isLocation;});
        if (it == nodes.end()) return NULL;
        else return *it;
    }

    PTNode* PTGraph::addNode(Value *v, bool isLocation) {
        PTNode* node = findValue(v, isLocation);
        if (node) return NULL;
        node = new PTNode(v, isLocation);
        nodes.push_back(node);
        // GlobalValue, AllocaInst, malloc always is
        // pointer and placeholder.
        CallSite CS(v);
        bool isMalloc = (!!CS) && CS.getCalledFunction()->getName() == "malloc";
        if (isa<GlobalValue>(v) || isa<AllocaInst>(v) || isMalloc) {
            PTNode* pnode = new PTNode(v, !isLocation);
            nodes.push_back(pnode);
            if (!isLocation)
                addEdge(node, pnode);
            else
                addEdge(pnode, node);
        }
        return node;
    }

    // bool PTGraph::addEdge(Value *from, Value *to) {
    //     PTNode* fromNode = findValue(from);
    //     PTNode* toNode = findValue(to);
    //     assert( fromNode && toNode && "Values should be created in this graph");
    //     return addEdge(fromNode, toNode);
    // }

    bool PTGraph::addEdge(PTNode *from, PTNode* to) {
        assert(std::find(nodes.begin(), nodes.end(), from) != nodes.end() && "fromnodes should be in this graph");
        assert(std::find(nodes.begin(), nodes.end(), to) != nodes.end() && "tonodes should be in this graph");
        if (std::find(from->next.begin(), from->next.end(), to) == from->next.end()) {
            from->next.push_back(to);
            return true;
        }
        return false;
    }

    // void PTGraph::onlyTracking(std::vector<Value*> values) {
    //     std::vector<PTNode*> trackNodes;
    //     std::for_each(values.begin(), values.end(), [&](Value* v) {
    //             PTNode* node = this->findValue(v);
    //             assert(node && "value should be created");
    //             trackNodes.push_back(node);
    //         });
    //     onlyTracking(trackNodes);
    // }

    void PTGraph::onlyTracking(std::vector<PTNode*>& trackNodes) {
        std::vector<PTNode*> workingList(trackNodes), markedList;
        while (!workingList.empty()) {
            PTNode* node = workingList.back();
            workingList.pop_back();
            markedList.push_back(node);
            for (auto neib : node->next) {
                if (std::find(markedList.begin(), markedList.end(), neib) != markedList.end())
                    continue;
                if (std::find(workingList.begin(), workingList.end(), neib) != workingList.end())
                    continue;
                workingList.push_back(neib);
            }
        }
        std::remove_if(nodes.begin(), nodes.end(), [markedList](const PTNode* node)->bool {
                return std::find(markedList.begin(), markedList.end(), node) == markedList.end();
            });
        for (auto nodep : nodes) {
            std::remove_if(nodep->next.begin(), nodep->next.end(), [markedList](const PTNode* node)->bool {
                    return std::find(markedList.begin(), markedList.end(), node) == markedList.end();
                });
        }
    }
};










