#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/MemorySSA.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include <set>
#include <map>

using namespace llvm;

typedef std::set<Instruction*>* InstSet;

namespace {
  class AstgrepPass : public FunctionPass {
  public:
    static char ID;
    AstgrepPass() : FunctionPass(ID) {}
    ~AstgrepPass() {}

    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  private:
    bool upAndMark(Instruction &inst, Instruction &liveInst);
    InstSet clobberingMemoryInstSet(MemoryAccess* MA, MemorySSAWalker* walker);
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {
  // instruction の前後で生存しているinstructionを保存(メモリに関するものだけ)
  // instruction じゃなっくて変数まで剥いてやったほうがはやそう
  // instruction からdubugLocを取得できる
  std::map<Instruction*, InstSet> instLiveBefore;
  std::map<Instruction*, InstSet> instLiveAfter;

  MemorySSA* MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  MemorySSAWalker* walker = MSSA->getWalker();

  // want to traverse all memory allocated variables
  // TODO: how to get all memory allocated variables?
  //  - Get all MemoryAccess by traversing and casting (to MA) instructions
  //  - get information from Basic Alias Analysis?

  // get all instruction in the Function
  std::list<Instruction *> works;
  for (inst_iterator inst_it = inst_begin(F); inst_it != inst_end(F); inst_it++) {
    works.push_back(&*inst_it);
  }

  while (!works.empty()) {
    Instruction* inst = works.back();
    works.pop_back();
    MemoryAccess* MA = MSSA->getMemoryAccess(inst);
    if (MA != nullptr) {
      if (!isa<MemoryUse>(MA)) {
        continue;
      }

      InstSet clobberingInsts = this->clobberingMemoryInstSet(MA, walker);
      for (auto inst = clobberingInsts->begin(); inst != clobberingInsts->end(); inst++) {
        errs() << "[clobbering]";
        (*inst)->dump();
      }

      // if memory access is MemoryUse
      // propagate live information of this instruction forward
      // MemoryUse* thisMemoryUse = dyn_cast<MemoryUse>(MA);
      // Instruction* thisInstruction = thisMemoryUse->getMemoryInst();
      // insert clobbering instruction
      // instLiveBefore[thisInstruction].insert()
    }
    inst->dump();
  }
  return false;
}

bool AstgrepPass::upAndMark(Instruction &inst, Instruction &liveInst) {
  return false;
}

InstSet AstgrepPass::clobberingMemoryInstSet(MemoryAccess* MA, MemorySSAWalker* walker) {
  // get all instruction of clobbering memory access

  InstSet instSet = new std::set<Instruction*>();

  // clobberingMemoryAccess should be MemoryPhi or MemoryDef
  // MemoryAccess* clobbering = walker->getClobberingMemoryAccess(MA);
  MemoryUseOrDef* memoryUseOrDef = dyn_cast<MemoryUseOrDef>(MA);
  MemoryPhi* memoryPhi = dyn_cast<MemoryPhi>(MA);

  /*
  if (instruction != nullptr && instruction->hasMetadata()) {
    instruction->getDebugLoc().dump();
  }
  */
  if (memoryUseOrDef != nullptr) {
    if (isa<MemoryDef>(memoryUseOrDef)) {
      Instruction* instruction = memoryUseOrDef->getMemoryInst();
      instSet->insert(instruction);
      return instSet;
    } else if (isa<MemoryUse>(memoryUseOrDef)) {
      MemoryAccess* clobbering = walker->getClobberingMemoryAccess(MA);
      InstSet clobbringInstSet = this->clobberingMemoryInstSet(clobbering, walker);
      for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
        instSet->insert(*inst);
      }
    }
  } else if (memoryPhi != nullptr) {
    for (unsigned int i = 0; i < memoryPhi->getNumOperands(); i++) {
      // get all incoming memory access
      MemoryAccess* incomingMA = memoryPhi->getIncomingValue(i);
      InstSet clobbringInstSet = this->clobberingMemoryInstSet(incomingMA, walker);
      for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
        instSet->insert(*inst);
      }
    }
  }
  return instSet;
}

char AstgrepPass::ID = 0;
static RegisterPass<AstgrepPass> X(
    "astgrep",
    "analysis for astgrep",
    false,
    false
);
