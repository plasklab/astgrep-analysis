#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/MemorySSA.h"
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

    // instruction の前後で生存しているinstructionを保存(メモリに関するものだけ)
    // instruction じゃなっくて変数まで剥いてやったほうがはやそう
    // instruction からdubugLocを取得できる
    std::map<Instruction*, InstSet> instLiveBefore;
    std::map<Instruction*, InstSet> instLiveAfter;

  private:
    void upAndMark(InstSet clobberingInsts, Instruction* useInst, BasicBlock* useBB);
    void upAndMarkRec(InstSet clobberingInsts, BasicBlock* bb);
    InstSet assembleClobberingMemoryInst(MemoryAccess* clobberingMA, MemorySSAWalker* walker);
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {

  MemorySSA* MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  MemorySSAWalker* walker = MSSA->getWalker();

  // want to traverse all memory allocated variables
  // TODO: how to get all memory allocated variables?
  //  - Get all MemoryAccess by traversing and casting (to MA) instructions
  //  - get information from Basic Alias Analysis?

  // initialize
  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock* bb = &*fit;
    errs() << "[basic block]";
    bb->dump();
    for(BasicBlock::iterator bbit = bb->begin(); bbit != bb->end(); bbit++) {
      Instruction* i = &*bbit;
      this->instLiveAfter[i] = new std::set<Instruction*>();
      this->instLiveBefore[i] = new std::set<Instruction*>();
    }
  }

  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock* bb = &*fit;
    for(BasicBlock::iterator bbit = bb->begin(); bbit != bb->end(); bbit++) {
      Instruction* inst = &*bbit;
      inst->dump();

      MemoryAccess* MA = MSSA->getMemoryAccess(inst);
      if (MA != nullptr) {
        MA->dump();
        if (!isa<MemoryUse>(MA)) {
          continue;
        }
        // if memory access is MemoryUse
        // propagate live information of clobbering instruction forward

        // get possible definitons
        MemoryAccess* clobberingMA = walker->getClobberingMemoryAccess(MA);
        InstSet clobberingInsts = this->assembleClobberingMemoryInst(clobberingMA, walker);
        errs() << "---clobberingInstsBegin---" << "\n";
        for (auto clobbering = clobberingInsts->begin(); clobbering != clobberingInsts->end(); clobbering++) {
          (*clobbering)->dump();
        }
        errs() << "---clobberingInstsEnd---" << "\n";

        // propagate live information of definitons forward from inst(use) to definitions
        this->upAndMark(clobberingInsts, inst, bb);
      }
    }
  }
  return false;
}

void AstgrepPass::upAndMark(InstSet clobberingInsts, Instruction* useInst, BasicBlock* useBB) {
  errs() << "---start---" << "\n";
  bool liveUseInst = false;
  for(BasicBlock::reverse_iterator bbit = useBB->rbegin();
      bbit != useBB->rend() && clobberingInsts->count(&*bbit) == 0;
      bbit++) {
    // TODO: include first instruction of basic block
    Instruction* i = &*bbit;
    if (liveUseInst) {
      i->dump();
      this->instLiveAfter[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
      this->instLiveBefore[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
    }
    if (i == useInst) {
      this->instLiveBefore[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
      liveUseInst = true;
    }
  }
  for (auto predBB = pred_begin(useBB); predBB != pred_end(useBB); predBB++) {
    this->upAndMarkRec(clobberingInsts, *predBB);
  }
  errs() << "---end---" << "\n";
}

void AstgrepPass::upAndMarkRec(InstSet clobberingInsts, BasicBlock* bb) {
  for(BasicBlock::reverse_iterator bbit = bb->rbegin();
      bbit != bb->rend() && clobberingInsts->count(&*bbit) == 0;
      bbit++) {
    Instruction* i = &*bbit;
    i->dump();
    this->instLiveAfter[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
    this->instLiveBefore[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
  }

  for (auto predBB = pred_begin(bb); predBB != pred_end(bb); predBB++) {
    this->upAndMarkRec(clobberingInsts, *predBB);
  }
}

InstSet AstgrepPass::assembleClobberingMemoryInst(MemoryAccess* clobberingMA, MemorySSAWalker* walker) {
  // assemble all instruction of clobbering memory access
  // if clobbering memory access is a memory def:
  //   return set which contain only the memory def instruction
  // else if memory access is a memory phi
  //   return set which contain all incoming memory access instruction

  InstSet instSet = new std::set<Instruction*>();

  // clobberingMemoryAccess should be MemoryPhi or MemoryDef
  MemoryUseOrDef* memoryUseOrDef = dyn_cast<MemoryUseOrDef>(clobberingMA);
  MemoryPhi* memoryPhi = dyn_cast<MemoryPhi>(clobberingMA);

  if (memoryUseOrDef != nullptr) {
    if (isa<MemoryDef>(memoryUseOrDef)) {
      Instruction *instruction = memoryUseOrDef->getMemoryInst();
      instSet->insert(instruction);
    }
  } else if (memoryPhi != nullptr) {
    for (unsigned int i = 0; i < memoryPhi->getNumOperands(); i++) {
      // get all incoming memory access
      MemoryAccess* incomingMA = memoryPhi->getIncomingValue(i);
      InstSet clobbringInstSet = this->assembleClobberingMemoryInst(incomingMA, walker);
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
