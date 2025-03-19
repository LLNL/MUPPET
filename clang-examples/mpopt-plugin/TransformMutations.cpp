#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <iomanip>

#include "utils.h"
#include "TransformMutations.h"

void TransformMutationsVisitor::ProcessEndOfBasicBlock(BasicBlockInfo* info, CompoundStmtIter* iter) {
    if (iter->basicBlockHead != iter->stmt->body_end()) {
        // create basic block between head and tail
        BasicBlockInfo basicBlock;
        basicBlock.blockType = BLOCK_TYPE_BASIC;

        CompoundStmt::const_body_iterator it = iter->basicBlockHead;
        CompoundStmt::const_body_iterator it_end = iter->basicBlockTail;

        SourceRange basicBlockRange;
        basicBlockRange.setBegin((*it)->getSourceRange().getBegin());
        basicBlockRange.setEnd((*it_end)->getSourceRange().getEnd());        
        basicBlock.range = basicBlockRange;

        do {
            // TODO: get function calls
            basicBlock.statements.push_back(CreateStatementInfo(*it));
            if (it != it_end)
                it++;
            else
                break;
        } while (true);

        info->blocks.push_back(basicBlock);

        // clear iter
        iter->basicBlockHead = iter->stmt->body_end();
        iter->basicBlockTail = iter->stmt->body_end();
    }
}

void TransformMutationsVisitor::ProcessCompoundStatement(BasicBlockInfo* parentInfo, BasicBlockInfo& basicBlock, const CompoundStmt* compoundStmt) {
    CompoundStmtIter iter;
    iter.stmt = compoundStmt;
    iter.basicBlockHead = compoundStmt->body_end();
    iter.basicBlockTail = compoundStmt->body_end();

    CompoundStmt::const_body_iterator it;
    for (it =compoundStmt->body_begin(); it != compoundStmt->body_end(); it++){
        ProcessStatement(&basicBlock, it, &iter);
    }

    ProcessEndOfBasicBlock(&basicBlock, &iter);
}

bool TransformMutationsVisitor::VisitStmt(Stmt* st) {
    if (!traversingSingleStatement)
        return true;    

    return true;
}

/*bool TransformMutationsVisitor::VisitStmt(Stmt* st) {
... collapsed
}*/

StatementInfo TransformMutationsVisitor::CreateStatementInfo(const Stmt* st) {
    StatementInfo info;
    info.stmt = st;
    info.range = st->getSourceRange();

    if (!isa<OMPExecutableDirective>(st))
        PrintStatement("Statement: ", st, astContext);

    // detect declarations, read/writes, etc. by traversing this single statement.
    // for example, aa = 1.0; can break down to
    //      * binary operation aa = 1.0
    //      * declaration reference expression aa
    //      * floating point literal 1.0
    traversingSingleStatement = &info;
    TraverseStmt((Stmt*)st);
    traversingSingleStatement = NULL;
    
    return info;
}

OMPMutation* TransformMutationsVisitor::IsPosEnabledForTransform(const Stmt* st, OMPMutationType type, OMPMutationPos pos) {
    SourceManager& sm = astContext->getSourceManager();
    unsigned int stStart = sm.getFileOffset(sm.getFileLoc(st->getBeginLoc()));
    unsigned int stEnd = sm.getFileOffset(sm.getFileLoc(st->getEndLoc()));    
    for (unsigned int i = 0; i < ompMutations.size(); i++) {
        if (ompMutations[i].rangeStart == stStart && 
            ompMutations[i].rangeEnd == stEnd &&
            ompMutations[i].pos == pos &&
            ompMutations[i].type == type) {
            return &ompMutations[i];
        }            
    }
    return NULL;
}

OMPMutation* TransformMutationsVisitor::IsPosEnabledForTransform(const OMPClause* clause, OMPMutationType type, OMPMutationPos pos) {
    SourceManager& sm = astContext->getSourceManager();
    unsigned int stStart = sm.getFileOffset(sm.getFileLoc(clause->getBeginLoc()));
    unsigned int stEnd = sm.getFileOffset(sm.getFileLoc(clause->getEndLoc()));    
    for (unsigned int i = 0; i < ompMutations.size(); i++) {
        if (ompMutations[i].rangeStart == stStart && 
            ompMutations[i].rangeEnd == stEnd &&
            ompMutations[i].pos == pos &&
            ompMutations[i].type == type) {
            return &ompMutations[i];
        }            
    }
    return NULL;
}

void TransformMutationsVisitor::ProcessTiling(const OMPExecutableDirective* ompDir) {
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, capturedStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

    if (forLoopCount >= 1) {
        unsigned int tileSize = 0;
        if (IsPosEnabledForTransform(ompDir, OMPMutationType::TILE_16, OMPMutationPos::INSERTAFTER)) {
            tileSize = 16;
        }
        else if (IsPosEnabledForTransform(ompDir, OMPMutationType::TILE_32, OMPMutationPos::INSERTAFTER)) {
            tileSize = 32;
        }
        else if (IsPosEnabledForTransform(ompDir, OMPMutationType::TILE_8, OMPMutationPos::INSERTAFTER)) {
            tileSize = 8;
        }

        if (tileSize > 0) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate tile 1");
            std::string tileText = "\n#pragma omp tile sizes(";
            tileText += std::to_string(tileSize);
            for (unsigned int i = 1; i < forLoopCount; i++) {
                tileText += "," + std::to_string(tileSize);
            }
            tileText += ")\n";
            rewriter.InsertTextAfter(ompDir->getEndLoc(), tileText);
            sourceChanged = true;
            isInsideTile = true;
        }       
    }
}

void TransformMutationsVisitor::ProcessTiling(const ForStmt* forStmt) {
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, true, forStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(forStmt, true, true);

    if (forLoopCount >= 1) {
        unsigned int tileSize = 0;
        if (IsPosEnabledForTransform(forStmt, OMPMutationType::TILE_16, OMPMutationPos::INSERTBEFORE)) {
            tileSize = 16;
        }
        else if (IsPosEnabledForTransform(forStmt, OMPMutationType::TILE_32, OMPMutationPos::INSERTBEFORE)) {
            tileSize = 32;
        }
        else if (IsPosEnabledForTransform(forStmt, OMPMutationType::TILE_8, OMPMutationPos::INSERTBEFORE)) {
            tileSize = 8;
        }

        if (tileSize > 0) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate tile 2");
            std::string tileText = "\n#pragma omp tile sizes(";
            tileText += std::to_string(tileSize);
            for (unsigned int i = 1; i < forLoopCount; i++) {
                tileText += "," + std::to_string(tileSize);
            }
            tileText += ")\n";
            rewriter.InsertText(forStmt->getBeginLoc(), tileText);
            sourceChanged = true;
            isInsideTile = true;
        }   
    }
}

void TransformMutationsVisitor::ProcessSIMD(const ForStmt* forStmt) {
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, forStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(forStmt, true, false);

    if (forLoopCount >= 1) {    
        if (IsPosEnabledForTransform(forStmt, OMPMutationType::SIMD, OMPMutationPos::INSERTBEFORE)) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate simd 1");

            std::string tileText = "#pragma omp simd\n";
            rewriter.InsertText(forStmt->getBeginLoc(), tileText);    
            sourceChanged = true;
            isInsideSIMD = true;
        }
    }
}

void TransformMutationsVisitor::ProcessSIMD(const OMPExecutableDirective* ompDir) {
    if (isa<OMPForDirective>(ompDir) || 
        isa<OMPParallelForDirective>(ompDir)) {

        const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
        const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
        if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, capturedStmt))
            return;
        unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, false, false);

        if (forLoopCount >= 1) {
            if (ompDir->getNumClauses() > 0) {
                if (IsPosEnabledForTransform(ompDir->getClause(0), OMPMutationType::SIMD, OMPMutationPos::INSERTBEFORE)) {
                    PRINT_DEBUG_MESSAGE("-> -> -> mutate simd 2");

                    rewriter.InsertText(ompDir->getClause(0)->getBeginLoc(), " simd ");
                    sourceChanged = true;
                    isInsideSIMD = true;
                }
            }
            else {
                if (IsPosEnabledForTransform(ompDir, OMPMutationType::SIMD, OMPMutationPos::INSERTAFTER)) {
                    PRINT_DEBUG_MESSAGE("-> -> -> mutate simd 3");

                    rewriter.InsertTextAfter(ompDir->getEndLoc(), " simd ");
                    sourceChanged = true;
                    isInsideSIMD = true;
                }
            }            
        }            
    }        
}

void TransformMutationsVisitor::ProcessCollapse(const OMPExecutableDirective* ompDir) {
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

    if (forLoopCount >= 2) {
        if (IsPosEnabledForTransform(ompDir, OMPMutationType::COLLAPSE, OMPMutationPos::INSERTAFTER)) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate collapse");

            std::string collapseText = " collapse(";
            collapseText += std::to_string(forLoopCount);
            collapseText += ")";
            rewriter.InsertTextAfter(ompDir->getEndLoc(), collapseText);
            sourceChanged = true;
            isInsideCollapse = true;
        }
    }  
}

void TransformMutationsVisitor::ProcessFirstPrivate(const OMPExecutableDirective* ompDir) {
    if (IsPosEnabledForTransform(ompDir, OMPMutationType::FIRSTPRIVATE, OMPMutationPos::REPLACE) ||
        IsPosEnabledForTransform(ompDir, OMPMutationType::FIRSTPRIVATE, OMPMutationPos::INSERTAFTER)) {
        if (isa<OMPParallelDirective>(ompDir) || isa<OMPForDirective>(ompDir) || 
            isa<OMPSectionsDirective>(ompDir) || isa<OMPParallelForDirective>(ompDir)) {
            const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
            const Stmt* capturedStmt = innermostStmt->getCapturedStmt();

            varDeclVisitor->clear();
            varDeclVisitor->TraverseStmt((Stmt*)capturedStmt);
            std::set<std::string> roVars = varDeclVisitor->getReadOnlyVars();
            if (varDeclVisitor->hasInnerFirstPrivate() || roVars.size() == 0) {
                return;
            }

            OMPSharedClause* sharedClause = nullptr;
            for (unsigned int i = 0; i < ompDir->getNumClauses(); i++) {
                if (OMPSharedClause* varList = dyn_cast<OMPSharedClause>(ompDir->getClause(i))) {
                    sharedClause = varList;
                }
                if (OMPPrivateClause* varList = dyn_cast<OMPPrivateClause>(ompDir->getClause(i))) {
                    for (ArrayRef<const Expr *>::iterator it = varList->varlist_begin(); it != varList->varlist_end(); it++) {
                        const Expr* varExpr = (const Expr*)(*it);
                        if (const DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(varExpr)) {
                            if (const VarDecl* varDecl = dyn_cast<VarDecl>(declRefExpr->getDecl())) {
                                roVars.erase(varDecl->getNameAsString());
                            }
                        }
                    }
                }                
                if (OMPReductionClause* varList = dyn_cast<OMPReductionClause>(ompDir->getClause(i))) {
                    for (ArrayRef<const Expr *>::iterator it = varList->varlist_begin(); it != varList->varlist_end(); it++) {
                        const Expr* varExpr = (const Expr*)(*it);
                        if (const DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(varExpr)) {
                            if (const VarDecl* varDecl = dyn_cast<VarDecl>(declRefExpr->getDecl())) {
                                roVars.erase(varDecl->getNameAsString());
                            }
                        }
                    }
                }
            }

            if (isInFirstPrivate)
                return;

            isInFirstPrivate = true;
            PRINT_DEBUG_MESSAGE("-> -> -> mutate firstprivate");
            if (sharedClause) {
                SourceRange clauseRange;
                clauseRange.setBegin(sharedClause->getBeginLoc());
                clauseRange.setEnd(sharedClause->getEndLoc());
                std::string clauseStr = "shared(";
                bool firstStr = true;
                for (ArrayRef<const Expr *>::iterator it = sharedClause->varlist_begin(); it != sharedClause->varlist_end(); it++) {
                    const Expr* varExpr = (const Expr*)(*it);
                    if (const DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(varExpr)) {
                        if (const VarDecl* varDecl = dyn_cast<VarDecl>(declRefExpr->getDecl())) {
                            if (roVars.find(varDecl->getNameAsString()) == roVars.end()) {
                                if (firstStr) {
                                    firstStr = false;
                                    clauseStr += varDecl->getNameAsString();
                                }
                                else {
                                    clauseStr += ",";
                                    clauseStr += varDecl->getNameAsString();
                                }                                
                            }
                        }
                    }
                }         
                if (firstStr) 
                    clauseStr = "firstprivate(";
                else
                    clauseStr += ") firstprivate(";

                firstStr = true;
                for (auto s : roVars) {
                    if (firstStr) {
                        firstStr = false;
                        clauseStr += s;
                    }
                    else {
                        clauseStr += ",";
                        clauseStr += s;
                    }
                }
                clauseStr += ")";

                rewriter.ReplaceText(clauseRange, clauseStr);
            }
            else {
                std::string clauseStr = " firstprivate(";
                bool firstStr = true;
                for (auto s : roVars) {
                    if (firstStr) {
                        firstStr = false;
                        clauseStr += s;
                    }
                    else {
                        clauseStr += ",";
                        clauseStr += s;
                    }
                }
                clauseStr += ")";

                rewriter.InsertTextAfter(ompDir->getEndLoc(), clauseStr);
            }
        }    
    }
}

void TransformMutationsVisitor::ProcessSchedule(const OMPExecutableDirective* ompDir) {
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

    if (forLoopCount >= 1) {
        if (IsPosEnabledForTransform(ompDir, OMPMutationType::SCHE_DYN, OMPMutationPos::INSERTAFTER)) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate schedule");

            std::string collapseText = " schedule(auto)";
            rewriter.InsertTextAfter(ompDir->getEndLoc(), collapseText);
            sourceChanged = true;
        }
        else if (IsPosEnabledForTransform(ompDir, OMPMutationType::SCHE_GUIDED, OMPMutationPos::INSERTAFTER)) {
            PRINT_DEBUG_MESSAGE("-> -> -> mutate schedule");

            std::string collapseText = " schedule(guided)";
            rewriter.InsertTextAfter(ompDir->getEndLoc(), collapseText);
            sourceChanged = true;
        }
    }  
}

void TransformMutationsVisitor::ProcessProcBind(const OMPExecutableDirective* ompDir) {
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
        return;

    if (IsPosEnabledForTransform(ompDir, OMPMutationType::PROC_BIND_CLOSE, OMPMutationPos::INSERTAFTER)) {
        PRINT_DEBUG_MESSAGE("-> -> -> mutate proc bind");

        std::string collapseText = " proc_bind(true)";
        rewriter.InsertTextAfter(ompDir->getEndLoc(), collapseText);
        sourceChanged = true;
    }
    else if (IsPosEnabledForTransform(ompDir, OMPMutationType::PROC_BIND_SPREAD, OMPMutationPos::INSERTAFTER)) {
        PRINT_DEBUG_MESSAGE("-> -> -> mutate proc bind");

        std::string collapseText = " proc_bind(spread)";
        rewriter.InsertTextAfter(ompDir->getEndLoc(), collapseText);
        sourceChanged = true;
    }
}

void TransformMutationsVisitor::ProcessStatement(BasicBlockInfo* info, CompoundStmt::const_body_iterator stmtIt, CompoundStmtIter* stmtIter) {
    const Stmt * st = *stmtIt;

    if (IsComplexStatement(st)) {
        ProcessEndOfBasicBlock(info, stmtIter);
    }

    if (isa<OMPExecutableDirective>(st)) {
        const OMPExecutableDirective* ompDir = dyn_cast<OMPExecutableDirective>(st);
        if (isa<OMPSingleDirective>(st) || isa<OMPCriticalDirective>(st) || 
            isa<OMPMasterDirective>(st) || isa<OMPBarrierDirective>(st) ||
            isa<OMPFlushDirective>(st) || isa<OMPAtomicDirective>(st))
            return;

        PRINT_DEBUG_MESSAGE("\tis an OMP block, has " << ompDir->getNumClauses() << " clauses");
        const Stmt* capturedStmt = ompDir->getInnermostCapturedStmt()->getCapturedStmt();
        PRINT_DEBUG_MESSAGE("capture source range:");
        PrintSourceRange(capturedStmt->getSourceRange(), astContext);

        BasicBlockInfo ompBB;
        ompBB.blockType = BLOCK_TYPE_OMP;
        SourceRange topRange(st->getBeginLoc(), capturedStmt->getEndLoc());
        ompBB.range = topRange;

        BasicBlockInfo dirBB;
        dirBB.blockType = BLOCK_TYPE_OMP_DIR;
        dirBB.range = st->getSourceRange();
        ompBB.blocks.push_back(dirBB);

        dirBB.statements.push_back(CreateStatementInfo(ompDir));

        bool prevIsInsideTile = isInsideTile;
        bool prevIsInsideSIMD = isInsideSIMD;
        bool prevIsInParallelLoop = isInParallelLoop;
        bool prevIsInFirstPrivate = isInFirstPrivate;
        bool prevIsInsideCollapse = isInsideCollapse;

        if (isa<OMPParallelDirective>(ompDir) || isa<OMPForDirective>(ompDir) || 
            isa<OMPSectionsDirective>(ompDir) || isa<OMPParallelForDirective>(ompDir))
            isInParallelLoop = true;

        for (unsigned int i = 0; i < ompDir->getNumClauses(); i++) {
            if (isa<OMPCollapseClause>(ompDir->getClause(i))) {
                isInsideCollapse = true;
            }
        }

        ProcessSIMD(ompDir);  
        ProcessFirstPrivate(ompDir);                      
        ProcessCollapse(ompDir);
        if (!isInsideSIMD) {    
            ProcessTiling(ompDir);
        }

        if (isa<CompoundStmt>(capturedStmt)) {
            PRINT_DEBUG_MESSAGE("\tis compound; most likely an individual {} block in an omp block");
            BasicBlockInfo capturedBB;
            capturedBB.blockType = BLOCK_TYPE_BASIC;
            capturedBB.range = capturedStmt->getSourceRange();

            const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(capturedStmt);
            ProcessCompoundStatement(&ompBB, capturedBB, compoundStmt);
            ompBB.blocks.push_back(capturedBB);            
        }
        else if (isa<ForStmt>(capturedStmt)) {
            PRINT_DEBUG_MESSAGE("\tfor statement in omp block");

            const ForStmt* forStmt = dyn_cast<ForStmt>(capturedStmt);
            const Stmt* loopInit = forStmt->getInit();
            const Expr* loopCond = forStmt->getCond();
            const Expr* loopInc = forStmt->getInc();

            BasicBlockInfo topBB;
            topBB.blockType = BLOCK_TYPE_LOOP;
            topBB.range = forStmt->getSourceRange();

            // process head
            BasicBlockInfo headBlock;
            headBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
            SourceRange forHeadRange;
            forHeadRange.setBegin(forStmt->getLParenLoc());
            forHeadRange.setEnd(forStmt->getRParenLoc());
            headBlock.range = forHeadRange;

            if (loopInit) headBlock.statements.push_back(CreateStatementInfo(loopInit));
            if (loopCond) headBlock.statements.push_back(CreateStatementInfo(loopCond));
            if (loopInc) headBlock.statements.push_back(CreateStatementInfo(loopInc));

            topBB.blocks.push_back(headBlock);

            // process body
            BasicBlockInfo basicBlock;
            basicBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
            basicBlock.range = forStmt->getBody()->getSourceRange();

            const Stmt* loopBody = forStmt->getBody();

            // compound for body;
            if (isa<CompoundStmt>(loopBody)) {
                const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(loopBody);
                ProcessCompoundStatement(info, basicBlock, compoundStmt);
            }
            // single for body;
            else {
                StatementInfo stmtInfo;
                stmtInfo.range = loopBody->getSourceRange();
                // TODO: get function calls
                basicBlock.statements.push_back(stmtInfo);
            }

            topBB.blocks.push_back(basicBlock);
            ompBB.blocks.push_back(topBB);
        }

        PRINT_DEBUG_MESSAGE("end of OMP block processing");
        PrintSourceRange(capturedStmt->getSourceRange(), astContext);

        isInsideTile = prevIsInsideTile;
        isInsideSIMD = prevIsInsideSIMD;
        isInParallelLoop = prevIsInParallelLoop;
        isInFirstPrivate = prevIsInFirstPrivate;
        isInsideCollapse = prevIsInsideCollapse;
    }
    else if (isa<CompoundStmt>(st)) {
        PRINT_DEBUG_MESSAGE("\tis compound; most likely an individual {} block");

        BasicBlockInfo basicBlock;
        basicBlock.blockType = BLOCK_TYPE_BASIC;
        basicBlock.range = st->getSourceRange();

        const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(st);
        ProcessCompoundStatement(info, basicBlock, compoundStmt);
        info->blocks.push_back(basicBlock);
    }
    else if (isa<ForStmt>(st)) {
        PRINT_DEBUG_MESSAGE("\tfor statement");

        const ForStmt* forStmt = dyn_cast<ForStmt>(st);
        const Stmt* loopInit = forStmt->getInit();
        const Expr* loopCond = forStmt->getCond();
        const Expr* loopInc = forStmt->getInc();

        bool prevIsInsideTile = isInsideTile;
        bool prevIsInsideSIMD = isInsideSIMD;
        bool prevIsInParallelLoop = isInParallelLoop;
        bool prevIsInFirstPrivate = isInFirstPrivate;
        bool prevIsInsideCollapse = isInsideCollapse;

        BasicBlockInfo topBB;
        topBB.blockType = BLOCK_TYPE_LOOP;
        topBB.range = forStmt->getSourceRange();

        // process head
        BasicBlockInfo headBlock;
        headBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        SourceRange forHeadRange;
        forHeadRange.setBegin(forStmt->getLParenLoc());
        forHeadRange.setEnd(forStmt->getRParenLoc());
        headBlock.range = forHeadRange;

        if (loopInit) headBlock.statements.push_back(CreateStatementInfo(loopInit));
        if (loopCond) headBlock.statements.push_back(CreateStatementInfo(loopCond));
        if (loopInc) headBlock.statements.push_back(CreateStatementInfo(loopInc));

        topBB.blocks.push_back(headBlock);

        // process body
        BasicBlockInfo basicBlock;
        basicBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        basicBlock.range = forStmt->getBody()->getSourceRange();

        const Stmt* loopBody = forStmt->getBody();

        PRINT_DEBUG_MESSAGE("\tcurrent for level: " << forNestLevel);        
        if (!isInsideTile && !isInsideCollapse)
            ProcessSIMD(forStmt);
        if (!isInsideSIMD)
            ProcessTiling(forStmt);

        // compound for body;
        if (isa<CompoundStmt>(loopBody)) {
            const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(loopBody);
            forNestLevel++;
            ProcessCompoundStatement(info, basicBlock, compoundStmt);
            forNestLevel--;
        }
        // single for body;
        else {
            StatementInfo stmtInfo;
            stmtInfo.range = loopBody->getSourceRange();
            // TODO: get function calls
            basicBlock.statements.push_back(stmtInfo);
        }
        
        topBB.blocks.push_back(basicBlock);
        info->blocks.push_back(topBB);

        isInsideTile = prevIsInsideTile;
        isInsideSIMD = prevIsInsideSIMD;
        isInParallelLoop = prevIsInParallelLoop;
        isInFirstPrivate = prevIsInFirstPrivate;
        isInsideCollapse = prevIsInsideCollapse;
    }
    else if (isa<DoStmt>(st)) {
        const DoStmt* doSt = dyn_cast<DoStmt>(st);
        //const Expr* cond = doSt->getCond();
        const Stmt* body = doSt->getBody();

        BasicBlockInfo topBB;
        topBB.blockType = BLOCK_TYPE_LOOP;
        topBB.range = doSt->getSourceRange();

        // body block
        BasicBlockInfo basicBlock;
        basicBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        basicBlock.range = body->getSourceRange();

        // compound do body;
        if (isa<CompoundStmt>(body)) {
            const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(body);
            ProcessCompoundStatement(info, basicBlock, compoundStmt);
        }
        // single do body;
        else {
            StatementInfo stmtInfo;
            stmtInfo.range = body->getSourceRange();
            // TODO: get function calls
            basicBlock.statements.push_back(stmtInfo);
        }
        topBB.blocks.push_back(basicBlock);

        // condition block
        BasicBlockInfo condBB;
        condBB.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        condBB.range = doSt->getCond()->getSourceRange();
        condBB.statements.push_back(CreateStatementInfo(doSt->getCond()));
        topBB.blocks.push_back(condBB);

        info->blocks.push_back(topBB);
    }
    else if (isa<WhileStmt>(st)) {
        const WhileStmt* whileStmt = dyn_cast<WhileStmt>(st);
        //const Expr* cond = whileStmt->getCond();
        const Stmt* body = whileStmt->getBody();

        BasicBlockInfo topBB;
        topBB.blockType = BLOCK_TYPE_LOOP;
        topBB.range = whileStmt->getSourceRange();

        // condition block
        BasicBlockInfo condBB;
        condBB.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        condBB.range = whileStmt->getCond()->getSourceRange();
        condBB.statements.push_back(CreateStatementInfo(whileStmt->getCond()));

        topBB.blocks.push_back(condBB);

        // body block
        BasicBlockInfo basicBlock;
        basicBlock.blockType = BLOCK_TYPE_LOOP_COMPONENT;
        basicBlock.range = body->getSourceRange();

        // compound do body;
        if (isa<CompoundStmt>(body)) {
            const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(body);
            ProcessCompoundStatement(info, basicBlock, compoundStmt);
        }
        // single do body;
        else {
            StatementInfo stmtInfo;
            stmtInfo.range = body->getSourceRange();
            // TODO: get function calls
            basicBlock.statements.push_back(stmtInfo);
        }
        
        topBB.blocks.push_back(basicBlock);
        info->blocks.push_back(topBB);
    }
    else if (isa<IfStmt>(st)) {        
        const IfStmt* ifSt = dyn_cast<IfStmt>(st);
        const Stmt* thenBody = ifSt->getThen();
        const Stmt* elseBody = ifSt->getElse();

        // create a parent basic block for if/elif/else
        BasicBlockInfo topBB;
        topBB.blockType = BLOCK_TYPE_COND;
        topBB.range = ifSt->getSourceRange();

        PRINT_DEBUG_MESSAGE("\tif statement: " << thenBody << ", " << elseBody);

        // treat it as three basic blocks
        // if block
        BasicBlockInfo condBB;
        condBB.blockType = BLOCK_TYPE_COND_COMPONENT;
        condBB.range = ifSt->getCond()->getSourceRange();
        condBB.statements.push_back(CreateStatementInfo(ifSt->getCond()));

        topBB.blocks.push_back(condBB);

        // then block
        if (thenBody) {
            BasicBlockInfo thenBB;
            thenBB.blockType = BLOCK_TYPE_COND_COMPONENT;
            thenBB.range = thenBody->getSourceRange();

            // compound then        
            if (isa<CompoundStmt>(thenBody)) {
                const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(thenBody);
                ProcessCompoundStatement(info, thenBB, compoundStmt);
            }
            // single then
            else {
                thenBB.statements.push_back(CreateStatementInfo(thenBody));
            }

            topBB.blocks.push_back(thenBB);
        }

        // else block (if it exists)
        if (elseBody) {
            BasicBlockInfo elseBB;
            elseBB.blockType = BLOCK_TYPE_COND_COMPONENT;
            elseBB.range = elseBody->getSourceRange();

            // compound then        
            if (isa<CompoundStmt>(elseBody)) {
                const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(elseBody);
                ProcessCompoundStatement(info, elseBB, compoundStmt);
            }
            // single then
            else {
                elseBB.statements.push_back(CreateStatementInfo(elseBody));
            }

            topBB.blocks.push_back(elseBB);
        }
        info->blocks.push_back(topBB);
    }
    else if (isa<SwitchStmt>(st)) {
        PRINT_DEBUG_MESSAGE("\tswitch/case statement");
        const SwitchStmt* swStmt = dyn_cast<SwitchStmt>(st);
        // TODO: since switch condition is an integer expression, its priority is VERY low.
        BasicBlockInfo topBB;
        topBB.blockType = BLOCK_TYPE_COND;
        topBB.range = swStmt->getSourceRange();

        // iterate between switch cases
        const SwitchCase* casePos = swStmt->getSwitchCaseList();
        while (casePos) {
            const Stmt* subStmt = casePos->getSubStmt();
            BasicBlockInfo caseBB;
            caseBB.blockType = BLOCK_TYPE_COND_COMPONENT;
            caseBB.range = subStmt->getSourceRange();

            if (isa<CompoundStmt>(subStmt)) {
                const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(subStmt);
                ProcessCompoundStatement(info, caseBB, compoundStmt);                
            }
            else {
                caseBB.statements.push_back(CreateStatementInfo(subStmt));
            }

            topBB.blocks.push_back(caseBB);

            casePos = casePos->getNextSwitchCase();
        }
        info->blocks.push_back(topBB);
    }
    else {
        // regular statement
        StatementInfo stInfo;
        stInfo.range = st->getSourceRange();
        // TODO: function calls

        if (stmtIter->basicBlockHead == stmtIter->stmt->body_end()) {
            stmtIter->basicBlockHead = stmtIt;
        }
        stmtIter->basicBlockTail = stmtIt;
    }
}

bool TransformMutationsVisitor::VisitFunctionDecl(FunctionDecl* func) {
    auto loc1 = func->getLocation();
    FileID id = rewriter.getSourceMgr().getMainFileID();    
    string originalFilename = astContext->getSourceManager().getFilename(astContext->getSourceManager().getLocForStartOfFile(id)).str();
    // check against a list of excluded directories
    for (size_t i = 0; i < sizeof(sSystemIncludeDirectories) / sizeof(const char*); i++) {
        string fileName = astContext->getSourceManager().getFilename(loc1).str();
        if (fileName == "" || fileName.find(sSystemIncludeDirectories[i]) != std::string::npos)
            return true;
        if (fileName != originalFilename)
            return true;
    }

    if (!func->doesThisDeclarationHaveABody())
        return true;

    forNestLevel = 0;

    if (funcInfos.find(func->getNameInfo().getName().getAsString()) == funcInfos.end()) {
        FunctionInfo* info = new FunctionInfo();
        const CompoundStmt *fBody = dyn_cast<CompoundStmt>(func->getBody());
        info->name = func->getNameInfo().getName().getAsString();
        info->range = fBody->getSourceRange();
        info->blockType = BLOCK_TYPE_FUNC;
        funcInfos[info->name] = info;

        PRINT_DEBUG_MESSAGE("Function Name: " << info->name);
        
        CompoundStmt::const_body_iterator stmtIt;
        CompoundStmtIter iter;
        iter.stmt = fBody;
        iter.basicBlockHead = fBody->body_end();
        iter.basicBlockTail = fBody->body_end();
        
        for (stmtIt =fBody->body_begin(); stmtIt != fBody->body_end(); stmtIt++){
            ProcessStatement(info, stmtIt, &iter);
        }

        ProcessEndOfBasicBlock(info, &iter);

        /* OutputFunctionInfo() call collapsed */
    }
    return true;
}

/* OutputFunctionInfo() function collapsed */

void TransformMutationsVisitor::SetupFileStart() { 
        fileStart = astContext->getSourceManager().getLocForStartOfFile(astContext->getSourceManager().getMainFileID());  
        PrintSourceLocation(fileStart, astContext);
    }

void TransformMutationsVisitor::ImportOMPMutations() {
    ifstream funcJsonFile;
    std::string tmpDir = g_pluginRoot + "/workspace/func_analysis/";
    std::string jsonFileName = tmpDir + g_dirName + ".json";
    funcJsonFile.open(jsonFileName);
    if (funcJsonFile.is_open()) {
        json j;
        funcJsonFile >> j;
        for (unsigned int i = 0; i < j["list"].size(); i++) {
            if (j["list"][i]["enabled"]) {
                OMPMutation mutation;
                mutation.pos = (OMPMutationPos)(j["list"][i]["pos"]);
                mutation.type = (OMPMutationType)(j["list"][i]["type"]);
                mutation.rangeStart = (unsigned int)j["list"][i]["range_start"];
                mutation.rangeEnd = (unsigned int)j["list"][i]["range_end"];
                ompMutations.push_back(mutation);                
            }
        }
        PRINT_DEBUG_MESSAGE("mutation import successfully - " << jsonFileName << ", total " << ompMutations.size());
        funcJsonFile.close();
    }
    else {
        PRINT_DEBUG_MESSAGE("error - file not exist - " << jsonFileName);
    }    
}

void TransformMutationsASTConsumer::HandleTranslationUnit(ASTContext &Context) {
    // insert macro at the beginning of the file
    FileID id = rewriter.getSourceMgr().getMainFileID();    
    string originalFilename = rewriter.getSourceMgr().getFilename(rewriter.getSourceMgr().getLocForStartOfFile(id)).str();
    // write original file info to file
    string marker = "// " + originalFilename + "\n";

    std::string basefilename = basename(originalFilename);
    string filename = g_pluginRoot + "/workspace/original_files/" + basefilename;

    visitor->SetupFileStart();
    visitor->ImportOMPMutations();
    visitor->TraverseDecl(Context.getTranslationUnitDecl());

    // save original files (but with a marker)
    if (visitor->IsSourceChanged())
    {
        //rewriter.InsertText(rewriter.getSourceMgr().getLocForStartOfFile(id), marker);

        // Create an output file to write the updated code
        std::error_code OutErrorInfo;
        std::error_code ok;
        const RewriteBuffer *RewriteBuf = rewriter.getRewriteBufferFor(id);
        if (RewriteBuf) {
            llvm::raw_fd_ostream outFile(llvm::StringRef(originalFilename),
                OutErrorInfo, llvm::sys::fs::OF_None);
            if (OutErrorInfo == ok) {
                outFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
                PRINT_DEBUG_MESSAGE("Output file created - " << originalFilename);
            } else {
                PRINT_DEBUG_MESSAGE("Could not create file - " << originalFilename);
            }
        }
    }
}

unique_ptr<ASTConsumer> PluginTransformMutationsAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    PRINT_DEBUG_MESSAGE("Filename: " << file.str());

    g_pluginRoot = getenv("PLUGIN_RUN_ROOT");
    PRINT_DEBUG_MESSAGE("Plugin root: " << g_pluginRoot);
    g_mainFilename = file.str();
    g_dirName = basename(g_mainFilename);
    size_t dotpos = g_dirName.find(".");
    if (dotpos != std::string::npos)
        g_dirName = g_dirName.replace(dotpos, 1, "_");
    std::string tmpDir = g_pluginRoot + "/workspace/func_analysis/";
    mkdir((tmpDir + g_dirName).c_str(), 0777);

    if (CI.getLangOpts().LangStd == LangStandard::Kind::lang_cuda) {
        PRINT_DEBUG_MESSAGE("CUDA language detected");
        std::string basefilename = basename(g_mainFilename);
        string markerFileName = "./plugin_run_fa_";
        markerFileName = markerFileName + basefilename;
        ifstream pluginRunFile(markerFileName);
        if (pluginRunFile.is_open()) {
            pluginRunFile.close();
            remove(markerFileName.c_str());
            exit(0);
        }
        else {
            ofstream pluginRunFile(markerFileName);
            pluginRunFile << "touch" << std::endl;
            pluginRunFile.close();
        }
    }

    return make_unique<TransformMutationsASTConsumer>(&CI);
}
 
bool PluginTransformMutationsAction::ParseArgs(const CompilerInstance &CI, const vector<string> &args) {
    return true;
}