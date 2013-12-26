#include "llvm/ACT13/PTGraph.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CallSite.h"
#include <algorithm>
namespace ACT {
    bool PTGraph::merge(PTGraph& from) {
        bool modified = false;
        // first, creating missed nodes
        for (auto theirnode : from.nodes) {
            Value *v = theirnode->getValue();
            if (!findValue(v, theirnode->isLocation())) {
                modified |= (addNode(v, theirnode->isLocation()) != NULL);
            }
        }
        // then we could add missing edges
        for (auto theirnode : from.nodes) {
            PTNode* mynode = findValue(theirnode->getValue(), theirnode->isLocation());
            assert(mynode && "all missed nodes should be created");
            for (auto neiberhood : theirnode->next) {
                //errs() << "mynode: " << *mynode << "\n";
                //errs() << "neiberhood: " << *neiberhood << "\n";
                modified |= addEdge(mynode, findValue(neiberhood->getValue(), neiberhood->isLocation()));
            }
        }
        return modified;
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

    PTGraph *PTGraph::clone() const {
        // clone nodes
        PTGraph *result = new PTGraph();
        for (auto nodep : nodes) {
            result->addNode(nodep->getValue(), nodep->isLocation());
        }
        // clone edges
        for (auto nodep : nodes) {
            PTNode* anodep = result->findValue(nodep->getValue(), nodep->isLocation());
            for (auto nodepadj : nodep->next) {
                PTNode* anodepadj = result->findValue(nodepadj->getValue(), nodepadj->isLocation());
                assert(anodep && anodepadj && "all nodes should be created");
                result->addEdge(anodep, anodepadj);
            }
        }
        return result;
    }

    void PTGraph::clear() {
        for (auto nodep : nodes)
            delete nodep;
        nodes.clear();
    }

    bool PTGraph::identicalTo(const PTGraph *x) const {
        for (auto nodep : nodes) {
            auto it = std::find_if(x->nodes.begin(), x->nodes.end(), [&](const PTNode* anodep) {
                    return anodep->identicalTo(nodep);
                });
            if (it == x->nodes.end())
                return false;
            PTNode *anodep = *it;
            for (auto adjp : nodep->next) {
                auto adjit = std::find_if(anodep->next.begin(), anodep->next.end(), [&](const PTNode* aadjp) {
                        return aadjp->identicalTo(adjp);
                    });
                if (adjit == anodep->next.end())
                    return false;
            }
        }
        return true;
    }

    bool PTGraph::addEdge(PTNode *from, PTNode* to) {
        assert(!(from->getValue()->getName() == "count1" && to->getValue()->getName() == "count1" && !from->isLocation() && !to->isLocation()));
        assert(std::find(nodes.begin(), nodes.end(), from) != nodes.end() && "fromnodes should be in this graph");
        assert(std::find(nodes.begin(), nodes.end(), to) != nodes.end() && "tonodes should be in this graph");
        if (std::find(from->next.begin(), from->next.end(), to) == from->next.end()) {
            from->next.push_back(to);
            return true;
        }
        return false;
    }

    void PTGraph::onlyTracking(std::vector<PTNode*>& trackNodes) {
        errs() << "before onlyTracking, size = " << nodes.size() << "\n";

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
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [markedList](const PTNode* node)->bool {
                return std::find(markedList.begin(), markedList.end(), node) == markedList.end();
                }), nodes.end());
        for (auto nodep : nodes) {
            nodep->next.erase(std::remove_if(nodep->next.begin(), nodep->next.end(), [markedList](const PTNode* node)->bool {
                    return std::find(markedList.begin(), markedList.end(), node) == markedList.end();
                    }), nodep->next.end());
        }
        errs() << "after onlyTracking, size = " << nodes.size() << "\n";
    }
};
