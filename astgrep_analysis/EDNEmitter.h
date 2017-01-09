#ifndef PROJECT_EDNEMITTER_H
#define PROJECT_EDNEMITTER_H

#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

typedef std::set<const Value*>* ValueSet;

class EDNEmitter {
public:
  EDNEmitter(
    std::map<Instruction*, ValueSet> instLiveBefore,
    std::map<Instruction*, ValueSet> instLiveAfter,
    std::map<const Value*, const DbgDeclareInst*> dbgDeclareInstOf
  );

  std::string getLiveInfo(Instruction* inst);
private:
  std::map<Instruction*, ValueSet> instLiveBefore;
  std::map<Instruction*, ValueSet> instLiveAfter;
  std::map<const Value*, const DbgDeclareInst*> dbgDeclareInstOf;
  std::string getLiveVariablesEdn(ValueSet valueSet);
};

#endif //PROJECT_EDNEMITTER_H
