#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/MemorySSA.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"

using namespace llvm;

namespace {
  class AstgrepPass : public FunctionPass {
  public:
    static char ID;
    AstgrepPass() : FunctionPass(ID) {}
    ~AstgrepPass() {}

    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {
  MemorySSA* MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();

  // get all instruction in the Function
  std::list<Instruction *> works;
  for (inst_iterator inst_it = inst_begin(F); inst_it != inst_end(F); inst_it++) {
    works.push_back(&*inst_it);
  }
  while (!works.empty()) {
    Instruction* inst = works.front();
    works.pop_front();

    MemoryAccess* MA = MSSA->getMemoryAccess(inst);
    inst->dump();
    if (MA != nullptr) {
      for (memoryaccess_def_iterator defs_it = MA->defs_begin(); defs_it != MA->defs_end(); defs_it++) {
        // errs() << "def: " << **defs_it << "\n";
        MemoryUseOrDef* memoryUseOrDef = dyn_cast<MemoryUseOrDef>(*defs_it);
        if (memoryUseOrDef != nullptr) {
          Instruction* instruction = memoryUseOrDef->getMemoryInst();
          if (instruction != nullptr) {
            instruction->dump();
          }
        }
      }
    }
    errs() << "\n";
  }

  return false;
}

char AstgrepPass::ID = 0;
static RegisterPass<AstgrepPass> X(
    "astgrep",
    "analysis for astgrep",
    false,
    false
);
