#ifndef PTGRAPH_H
#define PTGRAPH_H
#include "llvm/IR/Value.h"
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
                OS << "("<< this << ")" << "node for " << value << "," << *value << "\n";
            else
                OS << "("<< this << ")" << "location in " << value << "," << *value << "\n";
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

        inline void print(raw_ostream& OS) {
            OS << "graph has " << nodes.size() << "nodes\n";
            for (auto nodep : nodes)
                nodep->print(OS);
        }
    };

}
#endif
