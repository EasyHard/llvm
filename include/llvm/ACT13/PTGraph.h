/**
 * This file provides a direct-graph data struct for point-to analysis result
 * of FlowtoAnalysis. Each node, PTNode in a graph may be pointer or
 * location. Edge from node A to node B means that the pointer or location
 * that node A representing may contain a pointer of B.
 * The pointer or locations a node representing should be checked by
 * PTNode::getValue() and PTNode::isLocation(). And adjs of a node
 * are listed in PTNode::next.
 **/
#ifndef PTGRAPH_H
#define PTGRAPH_H
#include "llvm/IR/Value.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Argument.h"
#include <vector>
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
namespace ACT {
    struct PTGraph;

    struct PTNode {

        std::vector<PTNode*> next;
        Value* value;
        bool location;
    PTNode(Value *v, bool l = false): value(v),location(l) {};
        inline Value* getValue() const {
            return value;
        }

        inline void print(raw_ostream &OS) {
            if (!isLocation())
                OS << "("<< this << ")" << "node for " << value << ",";
            else
                OS << "("<< this << ")" << "location in " << value << ",";
            if (!isa<Function>(value)) {
                OS << *value;
            }
            if (isa<Instruction>(value)) {
                Instruction* inst = cast<Instruction>(value);
                OS  << " in function "<< inst->getParent()->getParent()->getName();
            } else if (isa<Argument>(value)) {
                Argument *arg = cast<Argument>(value);
                OS  << " in function "<< arg->getParent()->getName();
            } else if (isa<Function>(value)) {
                Function *func = cast<Function>(value);
                OS << " function " << func->getName();
            }
            OS << "\n";
            for (auto nodep : next) {
                OS << nodep;
                if (nodep->isLocation())
                    OS << "LOC";
                OS << ", ";
            }
            OS << "\n";
        }

        inline bool isLocation() const {
            return location;
        }

        /**
         * Check if is identical to another node.
         **/
        inline bool identicalTo(const PTNode *node) const {
            return getValue() == node->getValue() && isLocation() == node->isLocation();
        }
    };

    struct PTGraph {
        std::vector<PTNode*> nodes;
        /**
         * Merge another graph, which means adding nodes and edges that
         * exist in `mergeFrom` in the current graph.
         **/
        void merge(PTGraph &mergeFrom);
        /**
         * Add edge for from->to. `from` and `to` should be nodes in this graph
         * and return true if the edge is not existed before adding.
         **/
        bool addEdge(PTNode *from, PTNode *to);
        /**
         * Add node to this graph.
         * Return a pointer to the added node if the node is not existed before.
         * NULL otherwise.
         **/
        PTNode* addNode(Value *v, bool isLocation = false);
        /**
         * Return node that representing the value as a pointer or location
         * return NULL if can not find one
         **/
        PTNode* findValue(Value *v, bool isLocation = false);
        /**
         * Shortcut function. If can not find, created one and set modified to true
         **/
        inline PTNode* findOrCreateValue(Value *v, bool isLocation, bool *modified) {
            PTNode *node = findValue(v, isLocation);
            if (node) {
                *modified = false;
                return node;
            }
            *modified = true;
            return addNode(v, isLocation);
        }
        /**
         * Only keep nodes that can be traveled from the given list
         **/
        void onlyTracking(std::vector<PTNode*>&);

        /**
         * Clone a new grpah that the same as it, but with seperate memory.
         **/
        PTGraph *clone() const;

        /**
         * Clear the graph and release all memory of nodes.
         **/
        void clear();

        /**
         * Check if is identical to the other graph. i.e. same node and
         * same edge.
         **/
        bool identicalTo(const PTGraph* graph) const;

        inline void print(raw_ostream& OS) {
            OS << "graph has " << nodes.size() << "nodes\n";
            for (auto nodep : nodes)
                nodep->print(OS);
        }
    };

}
#endif
