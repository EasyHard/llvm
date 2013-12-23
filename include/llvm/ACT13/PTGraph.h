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
                OS << "("<< this << ")" << "node for " << value << "," << *value ;
            else
                OS << "("<< this << ")" << "location in " << value << "," << *value ;
            if (isa<Instruction>(value)) {
                Instruction* inst = cast<Instruction>(value);
                OS << " in function "<< inst->getParent()->getParent()->getName();
            } else if (isa<Argument>(value)) {
                Argument *arg = cast<Argument>(value);
                OS << " in function "<< arg->getParent()->getName();
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
        void merge(PTGraph &mergeFrom);

        bool addEdge(PTNode *from, PTNode *to);

        PTNode* addNode(Value *v, bool isLocation = false);

        PTNode* findValue(Value *v, bool isLocation = false);

        inline PTNode* findOrCreateValue(Value *v, bool isLocation, bool *modified) {
            PTNode *node = findValue(v, isLocation);
            if (node) {
                *modified = false;
                return node;
            }
            *modified = true;
            return addNode(v, isLocation);
        }

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
