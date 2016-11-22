#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/MemorySSA.h"

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
    void dumpInstSet(InstSet instSet);

    // contain instructions which lives before/after a instruction
    // instruction じゃなっくて変数まで剥いてやったほうがはやそう
    std::map<Instruction*, InstSet> instLiveBefore;
    std::map<Instruction*, InstSet> instLiveAfter;

  private:
    void upAndMark(InstSet clobberingInsts, Instruction* useInst, BasicBlock* useBB);
    void upAndMarkRec(InstSet clobberingInsts, BasicBlock* bb);
    InstSet assembleClobberingMemoryInst(MemoryAccess* clobberingMA, MemorySSAWalker* walker);
    void dumpLiveInfo(Function* F);
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {

  MemorySSA* MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  MemorySSAWalker* walker = MSSA->getWalker();

  // initialize
  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock* bb = &*fit;
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
        this->dumpInstSet(clobberingInsts);
        errs() << "---clobberingInstsEnd---" << "\n";

        // propagate live information of definitons forward from inst(use) to definitions
        this->upAndMark(clobberingInsts, inst, bb);
      }
    }
  }
  // this->dumpLiveInfo(&F);
  return false;
}

void AstgrepPass::upAndMark(InstSet clobberingInsts, Instruction* useInst, BasicBlock* useBB) {
  /**
   * TODO: do not propate all Memoary Access for MemoryPhi
   *       into both predecessor basic blocks
   **/
  errs() << "---start---" << "\n";
  bool liveUseInst = false;
  for(BasicBlock::reverse_iterator bbit = useBB->rbegin();
      bbit != useBB->rend() && clobberingInsts->count(&*bbit) == 0;
      bbit++) {
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

void AstgrepPass::dumpInstSet(InstSet instSet) {
  for (auto inst = instSet->begin(); inst != instSet->end(); inst++) {
    (*inst)->dump();
  }
}

void AstgrepPass::dumpLiveInfo(Function* F){
  for(Function::iterator fit = F->begin(); fit != F->end(); fit++) {
    for (BasicBlock::iterator bbit = (&*fit)->begin(); bbit != (&*fit)->end(); bbit++) {
      Instruction* inst = &*bbit;
      errs() << "live before start" << "\n";
      this->dumpInstSet(this->instLiveBefore[inst]);
      errs() << "live before end" << "\n";
      inst->dump();
      errs() << "live after start" << "\n";
      this->dumpInstSet(this->instLiveAfter[inst]);
      errs() << "live after end" << "\n";
    }
  }
}

char AstgrepPass::ID = 0;
static RegisterPass<AstgrepPass> X(
    "astgrep",
    "analysis for astgrep",
    false,
    false
);
