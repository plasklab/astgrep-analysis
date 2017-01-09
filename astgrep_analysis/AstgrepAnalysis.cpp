#include <string>
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/Transforms/Utils/MemorySSA.h"

#include "EDNEmitter.h"

using namespace llvm;

typedef std::set<Instruction*>* InstSet;
typedef std::set<const Value*>* ValueSet;

namespace {
  class AstgrepPass : public FunctionPass {
  public:
    static char ID;
    AstgrepPass() : FunctionPass(ID) {}
    ~AstgrepPass() {}

    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    void dumpInstSet(InstSet instSet);
    void dumpValueSet(ValueSet valueSet);

    std::map<Instruction*, ValueSet> instLiveBefore;
    std::map<Instruction*, ValueSet> instLiveAfter;
    std::map<const Value*, const DbgDeclareInst*> dbgDeclareInstOf;

  private:
    AAResults* AAR;
    MemorySSA* MSSA;
    MemorySSAWalker* walker;

    void upAndMark(InstSet clobberingInsts, const Value* value, Instruction* startInst, BasicBlock* startBB);
    void upAndMarkRec(InstSet clobberingInsts, const Value* value, BasicBlock* bb);
    InstSet assembleClobberingMemoryInst(MemoryAccess* clobberingMA, const MemoryLocation* tartgetLoc);
  };
}

void AstgrepPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
  AU.addRequired<AAResultsWrapperPass>();
}

bool AstgrepPass::runOnFunction(Function &F) {

  AAR = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  walker = MSSA->getWalker();

  // initialize
  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock* bb = &*fit;
    for(BasicBlock::iterator bbit = bb->begin(); bbit != bb->end(); bbit++) {
      Instruction* i = &*bbit;
      this->instLiveAfter[i] = new std::set<const Value*>();
      this->instLiveBefore[i] = new std::set<const Value*>();
      DbgDeclareInst* dbgInst = dyn_cast<DbgDeclareInst>(i);
      if (dbgInst != nullptr) {
        this->dbgDeclareInstOf[dbgInst->getAddress()] = dbgInst;
      }
    }
  }

  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock* bb = &*fit;
    for(BasicBlock::iterator bbit = bb->begin(); bbit != bb->end(); bbit++) {
      Instruction* inst = &*bbit;

      // 各MemoryUseについて処理
      MemoryAccess* MA = MSSA->getMemoryAccess(inst);
      if (MA != nullptr && isa<MemoryUse>(MA)) {
        // propagate live information of clobbering instruction forward

        // MemoryUse が使用する MemoryLocation を取得しておく
        MemoryLocation location = MemoryLocation::get(inst);
        const Value* value = location.Ptr;

        // MA の clobbering instruction の集合を見つける.
        InstSet clobberingInsts = this->assembleClobberingMemoryInst(MA, &location);

        /*
        errs() << "---clobberingInstsBegin---" << "\n";
        this->dumpInstSet(clobberingInsts);
        errs() << "---clobberingInstsEnd---" << "\n";
        */
        this->upAndMark(clobberingInsts, value, inst, bb);
      }
      /*
      inst->dump();
      errs() << "--- live before ---" << "\n";
      dumpValueSet(instLiveBefore[inst]);
      errs() << "--- live after ---" << "\n";
      dumpValueSet(instLiveAfter[inst]);
      errs() << "\n";
      */
    }
  }
  EDNEmitter* emitter = new EDNEmitter(instLiveBefore, instLiveAfter, dbgDeclareInstOf);
  errs() << "[";
  for(Function::iterator fit = F.begin(); fit != F.end(); fit++) {
    BasicBlock *bb = &*fit;
    for (BasicBlock::iterator bbit = bb->begin(); bbit != bb->end(); bbit++) {
      Instruction* i = &*bbit;
      std::string edn = emitter->getLiveInfo(i);
      errs() << edn;
    }
  }
  errs() << "]";

  return false;
}

void AstgrepPass::upAndMark(InstSet clobberingInsts, const Value* value, Instruction* startInst, BasicBlock* startBB) {
  //errs() << "---start---" << "\n";
  bool alive = false;
  for(BasicBlock::reverse_iterator bbit = startBB->rbegin();
      bbit != startBB->rend(); bbit++) {
    Instruction* i = &*bbit;
    if (alive) {
      this->instLiveAfter[i]->insert(value);
      this->instLiveBefore[i]->insert(value);
    }
    if (i == startInst) {
      this->instLiveBefore[i]->insert(value);
      alive = true;
    }
  }

  // CFG を更に前方に辿っていく
  for (auto predBB = pred_begin(startBB); predBB != pred_end(startBB); predBB++) {
    this->upAndMarkRec(clobberingInsts, value, *predBB);
  }
  //errs() << "---end---" << "\n";
}

void AstgrepPass::upAndMarkRec(InstSet clobberingInsts, const Value* value, BasicBlock* bb) {
  for(BasicBlock::reverse_iterator bbit = bb->rbegin();
      bbit != bb->rend(); bbit++) {
    // 基本ブロック先頭にたどり着くか、clobberingInsts のうちどれかにたどり着いたら終了
    Instruction* i = &*bbit;
    if (clobberingInsts->find(i) != clobberingInsts->end()) {
      // 定義命令までたどり着いた場合
      this->instLiveAfter[i]->insert(value);
      return;
    } else {
      this->instLiveAfter[i]->insert(value);
      this->instLiveBefore[i]->insert(value);
    }
  }

  for (auto predBB = pred_begin(bb); predBB != pred_end(bb); predBB++) {
    this->upAndMarkRec(clobberingInsts, value, *predBB);
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
    MemoryAccess* clobberingMA = walker->getClobberingMemoryAccess(MA, *targetLoc);

    InstSet clobbringInstSet = this->assembleClobberingMemoryInst(clobberingMA, targetLoc);
    for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
      instSet->insert(*inst);
    }
  } else if (memoryDef != nullptr) {
    // MemoryDef maybe call function
    Instruction *instruction = memoryDef->getMemoryInst();
    if (!isa<StoreInst>(instruction)) {
      // targetLoc なしで clobberingMemoryAccesss を取得する
      MemoryAccess* anotherClobberingMA = walker->getClobberingMemoryAccess(MA);
      InstSet clobbringInstSet = this->assembleClobberingMemoryInst(anotherClobberingMA, targetLoc);
      for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
        instSet->insert(*inst);
      }
    } else {

      // clobbering
      const MemoryLocation location = MemoryLocation::get(instruction);
      if (AAR->isMustAlias(location, *targetLoc)) {
        instSet->insert(instruction);
      } else {
        MemoryAccess* anotherClobberingMA = walker->getClobberingMemoryAccess(MA);
        InstSet clobbringInstSet = this->assembleClobberingMemoryInst(anotherClobberingMA, targetLoc);
        for (auto inst = clobbringInstSet->begin(); inst != clobbringInstSet->end(); inst++) {
          instSet->insert(*inst);
        }
      }
    }
  } else if (memoryPhi != nullptr) {
    for (unsigned int i = 0; i < memoryPhi->getNumOperands(); i++) {
      // MemoryPhi の各先行基本ブロックのうち、それぞれ最後の ClobberingMemoryAccess を取得
      MemoryAccess* incomingMA = memoryPhi->getIncomingValue(i);

      // incomingMA は必ずしも targetLoc に関する MemoryAccess とは限らない
      // もういっちょ clobberingMemoryAccess を取得
      // 上で MA が MemoryDef だったときに must alias か確認してるので不要かも
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

void AstgrepPass::dumpValueSet(ValueSet valueSet) {
  for (auto value = valueSet->begin(); value != valueSet->end(); value++) {
    if (this->dbgDeclareInstOf.find(*value) != this->dbgDeclareInstOf.end()) {
      dbgDeclareInstOf[*value]->dump();
      if (dbgDeclareInstOf[*value]->hasMetadata()) {
        DebugLoc loc = dbgDeclareInstOf[*value]->getDebugLoc();
        int line = loc.getLine();
        int col = loc.getCol();
        errs() << "line: " << line << " | col: " << col << "\n";
      } else {
      }
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
