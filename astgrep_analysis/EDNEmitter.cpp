#include <sstream>
#include <string>
#include <iostream>
#include <set>
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "EDNEmitter.h"

using namespace llvm;

typedef std::set<const Value*>* ValueSet;

EDNEmitter::EDNEmitter(
  std::map<Instruction*, ValueSet> instLiveBefore,
  std::map<Instruction*, ValueSet> instLiveAfter,
  std::map<const Value*, const DbgDeclareInst*> dbgDeclareInstOf
) {
  this->instLiveBefore = instLiveBefore;
  this->instLiveAfter = instLiveAfter;
  this->dbgDeclareInstOf = dbgDeclareInstOf;
}

std::string EDNEmitter::getLiveInfo(Instruction* inst) {
  std::ostringstream os;
  ValueSet liveBeforeValues = this->instLiveBefore[inst];
  ValueSet liveAfterValues = this->instLiveAfter[inst];

  if (!inst->hasMetadata()) {
    return "";
  }
  if (liveBeforeValues->size() == 0 && liveAfterValues->size() == 0) {
    return "";
  }

  os << "{" << "\n";
  os << ":line " << inst->getDebugLoc().getLine() << "\n";
  os << ":col " << inst->getDebugLoc().getCol() << "\n";

  // TODO: use edn library
  os << ":live_before " << this->getLiveVariablesEdn(liveBeforeValues) << "\n";
  os << ":live_after " << this->getLiveVariablesEdn(liveAfterValues) << "\n";
  os << "}" << "\n";
  return os.str();
}

std::string EDNEmitter::getLiveVariablesEdn(ValueSet valueSet) {
  std::ostringstream os;
  os << "[";
  for (auto value = valueSet->begin(); value != valueSet->end(); value++) {
    if (this->dbgDeclareInstOf.find(*value) != this->dbgDeclareInstOf.end() && dbgDeclareInstOf[*value]->hasMetadata()) {
      os << "{";
      DebugLoc loc = dbgDeclareInstOf[*value]->getDebugLoc();
      os << ":line " << loc.getLine() << " ";
      os << ":col " << loc.getCol();
      os << "}";
    }
  }
  os << "]";
  return os.str();
}
