#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
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

    std::map<Instruction*, InstSet> instLiveBefore;
    std::map<Instruction*, InstSet> instLiveAfter;

  private:
    MemorySSA* MSSA;
    MemorySSAWalker* walker;

    void upAndMark(InstSet clobberingInsts, Instruction* useInst, BasicBlock* useBB);
    void upAndMarkRec(InstSet clobberingInsts, BasicBlock* bb);
    InstSet assembleClobberingMemoryInst(MemoryAccess* clobberingMA, const MemoryLocation* tartgetLoc);
    void dumpLiveInfo(Function* F);
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {

  MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  walker = MSSA->getWalker();

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

      // 各MemoryUseについて処理
      MemoryAccess* MA = MSSA->getMemoryAccess(inst);
      if (MA != nullptr) {
        MA->dump();
        if (!isa<MemoryUse>(MA)) {
          continue;
        }
        // if memory access is MemoryUse
        // propagate live information of clobbering instruction forward

        // MemoryUse が使用する MemoryLocation を取得しておく
        MemoryLocation location = MemoryLocation::get(inst);

        // MA の clobbering instruction の集合を見つける.
        InstSet clobberingInsts = this->assembleClobberingMemoryInst(MA, &location);

        errs() << "---clobberingInstsBegin---" << "\n";
        this->dumpInstSet(clobberingInsts);
        errs() << "---clobberingInstsEnd---" << "\n";

        // propagate live information of definitons forward from inst(use) to definitions
        // TODO: instruction ではなくて、Value の生存情報を伝播させる。
        this->upAndMark(clobberingInsts, inst, bb);
      }
    }
  }
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
    // 基本ブロック先頭にたどり着くか、clobberingInsts のうちどれかにたどり着いたら終了
    Instruction* i = &*bbit;
    if (liveUseInst) {
      // i->dump();
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
    // 基本ブロック先頭にたどり着くか、clobberingInsts のうちどれかにたどり着いたら終了
    Instruction* i = &*bbit;
    // i->dump();
    this->instLiveAfter[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
    this->instLiveBefore[i]->insert(clobberingInsts->begin(), clobberingInsts->end());
  }

  for (auto predBB = pred_begin(bb); predBB != pred_end(bb); predBB++) {
    this->upAndMarkRec(clobberingInsts, *predBB);
  }
}

InstSet AstgrepPass::assembleClobberingMemoryInst(MemoryAccess* MA, const MemoryLocation* targetLoc) {
  /**
   * Assemble MemoryAccess's clobbering instructions
   */

  InstSet instSet = new std::set<Instruction*>();

  MemoryUse* memoryUse = dyn_cast<MemoryUse>(MA);
  MemoryDef* memoryDef = dyn_cast<MemoryDef>(MA);
  MemoryPhi* memoryPhi = dyn_cast<MemoryPhi>(MA);

  if (memoryUse != nullptr) {
    // TODO: check is it really (must alias of) MA
    MemoryAccess* clobberingMA = walker->getClobberingMemoryAccess(MA);

    InstSet clobbringInstSet = this->assembleClobberingMemoryInst(clobberingMA, targetLoc);
    for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
      instSet->insert(*inst);
    }
  } else if (memoryDef != nullptr) {
    Instruction *instruction = memoryDef->getMemoryInst();
    instSet->insert(instruction);
  } else if (memoryPhi != nullptr) {
    for (unsigned int i = 0; i < memoryPhi->getNumOperands(); i++) {
      // MemoryPhi の各先行基本ブロックのうち、それぞれ最後の ClobberingMemoryAccess を取得
      MemoryAccess* incomingMA = memoryPhi->getIncomingValue(i);

      // incomingMA は必ずしも targetLoc に関する MemoryAccess とは限らない
      // もういっちょ clobberingMemoryAccess を取得
      MemoryAccess* anotherClobbering = walker->getClobberingMemoryAccess(incomingMA, *targetLoc);
      InstSet clobbringInstSet = this->assembleClobberingMemoryInst(anotherClobbering, targetLoc);
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
