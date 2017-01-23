#include <sstream>
#include <string>
#include <iostream>
#include <set>
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "EDNEmitter.h"

using namespace llvm;

typedef std::set<const Value*>* ValueSet;
typedef std::set<Instruction*>* InstSet;

EDNEmitter::EDNEmitter(
  std::map<Instruction*, ValueSet> instLiveBefore,
  std::map<Instruction*, ValueSet> instLiveAfter,
  std::map<Instruction*, InstSet> reachingDefinitions,
  std::map<const Value*, const DbgDeclareInst*> dbgDeclareInstOf
) {
  this->instLiveBefore = instLiveBefore;
  this->instLiveAfter = instLiveAfter;
  this->reachingDefinitions = reachingDefinitions;
  this->dbgDeclareInstOf = dbgDeclareInstOf;
}

std::string EDNEmitter::getDataflowInfo(Instruction* inst) {
  std::ostringstream os;
  InstSet reachingInstructions = this->reachingDefinitions[inst];
  ValueSet liveBeforeValues = this->instLiveBefore[inst];
  ValueSet liveAfterValues = this->instLiveAfter[inst];

  if (!inst->hasMetadata()) {
    return "";
  }
  if (liveBeforeValues->size() == 0 && liveAfterValues->size() == 0 && reachingInstructions->size() == 0) {
    return "";
  }

  os << "{" << "\n";
  os << ":line " << inst->getDebugLoc().getLine() << "\n";
  os << ":col " << inst->getDebugLoc().getCol() << "\n";

  // TODO: use edn library
  os << ":before " << this->getLiveVariablesEdn(liveBeforeValues) << "\n";
  os << ":after " << this->getLiveVariablesEdn(liveAfterValues) << "\n";
  os << ":reach " << this->getReachingDefinitionsEdn(reachingInstructions) << "\n";
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
      os << "} ";
    }
  }
  os << "]";
  return os.str();
}

std::string EDNEmitter::getReachingDefinitionsEdn(InstSet instSet) {
  std::ostringstream os;
  os << "[";
  for (auto inst_it = instSet->begin(); inst_it != instSet->end(); inst_it++) {
    Instruction* inst = *inst_it;
    if (inst->hasMetadata()) {
      DebugLoc loc = inst->getDebugLoc();
      os << "{";
      os << ":line " << loc.getLine() << " ";
      os << ":col " << loc.getCol();
      os << "} ";
    }
  }
  os << "]";
  return os.str();
}
