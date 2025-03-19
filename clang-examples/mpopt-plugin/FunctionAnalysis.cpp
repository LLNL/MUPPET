#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <iomanip>

#include "utils.h"
#include "TransformMutations.h"
#include "FunctionAnalysis.h"

void FunctionAnalysisVisitor::ProcessEndOfBasicBlock(BasicBlockInfo* info, CompoundStmtIter* iter) {
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

void FunctionAnalysisVisitor::ProcessCompoundStatement(BasicBlockInfo* parentInfo, BasicBlockInfo& basicBlock, const CompoundStmt* compoundStmt) {
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

bool FunctionAnalysisVisitor::VisitStmt(Stmt* st) {
    if (!traversingSingleStatement)
        return true;    

    return true;
}

StatementInfo FunctionAnalysisVisitor::CreateStatementInfo(const Stmt* st) {
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

void FunctionAnalysisVisitor::AddMutation(SourceRange range, OMPMutationType type, OMPMutationPos pos) {
    OMPMutation mutation;
    mutation.pos = pos;
    mutation.type = type;
    mutation.range = range;
    ompMutations.push_back(mutation);    
    PRINT_DEBUG_MESSAGE("\t\tFOUND a Mutation point");
}

void FunctionAnalysisVisitor::ProcessTiling(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process tiling 1");
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, capturedStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

    if (!g_limitedMode && forLoopCount >= 1) {
        AddMutation(ompDir->getSourceRange(), OMPMutationType::TILE_8, OMPMutationPos::INSERTAFTER);
        AddMutation(ompDir->getSourceRange(), OMPMutationType::TILE_16, OMPMutationPos::INSERTAFTER);
        AddMutation(ompDir->getSourceRange(), OMPMutationType::TILE_32, OMPMutationPos::INSERTAFTER);
    }
}

void FunctionAnalysisVisitor::ProcessTiling(const ForStmt* forStmt) {
    PRINT_DEBUG_MESSAGE("process tiling 2");
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, true, forStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(forStmt, true, true);

    if (!g_limitedMode && forLoopCount >= 1) {
        AddMutation(forStmt->getSourceRange(), OMPMutationType::TILE_8, OMPMutationPos::INSERTBEFORE);                
        AddMutation(forStmt->getSourceRange(), OMPMutationType::TILE_16, OMPMutationPos::INSERTBEFORE);        
        AddMutation(forStmt->getSourceRange(), OMPMutationType::TILE_32, OMPMutationPos::INSERTBEFORE);                
    }
}

void FunctionAnalysisVisitor::ProcessSIMD(const ForStmt* forStmt) {
    PRINT_DEBUG_MESSAGE("process simd 1");
    if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, forStmt))
        return;
    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(forStmt, true, false);

    varDeclVisitor->clear();
    varDeclVisitor->TraverseStmt((Stmt*)forStmt);
    if (varDeclVisitor->hasInnerParallel()) {
        return;
    }
    if (forLoopCount >= 1) {
        AddMutation(forStmt->getSourceRange(), OMPMutationType::SIMD, OMPMutationPos::INSERTBEFORE);        
    }
}

void FunctionAnalysisVisitor::ProcessSIMD(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process simd 2");
    if (isa<OMPForDirective>(ompDir) || 
        isa<OMPParallelForDirective>(ompDir)) {

        const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
        const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
        if (forbiddenStmtVisitor->hasForbiddenStmt(true, false, capturedStmt))
            return;

        varDeclVisitor->clear();
        varDeclVisitor->TraverseStmt((Stmt*)capturedStmt);
        if (varDeclVisitor->hasInnerParallel()) {
            return;
        }

        unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, false, false);

        if (forLoopCount >= 1) {
            if (ompDir->getNumClauses() > 0) {
                AddMutation(ompDir->getClause(0)->getBeginLoc(), OMPMutationType::SIMD, OMPMutationPos::INSERTBEFORE);
            }
            else {
                AddMutation(ompDir->getSourceRange(), OMPMutationType::SIMD, OMPMutationPos::INSERTAFTER);
            }
        }
    }        
}

void FunctionAnalysisVisitor::ProcessCollapse(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process collapse");
    const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
    const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
    if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
        return;

    for (unsigned int i = 0; i < ompDir->getNumClauses(); i++) {
        if (OMPCollapseClause* collapse = dyn_cast<OMPCollapseClause>(ompDir->getClause(i))) {
            return;
        }
    }

    unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

    if (forLoopCount >= 2) {
        AddMutation(ompDir->getSourceRange(), OMPMutationType::COLLAPSE, OMPMutationPos::INSERTAFTER);        
    }  
}

void FunctionAnalysisVisitor::ProcessFirstPrivate(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process firstprivate");
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

        // if it's omp for, auto variables in the parent parallel region are all shared
        if (isa<OMPForDirective>(ompDir)) {
            return;
        }

        OMPFirstprivateClause* firstPrivateClause = nullptr;
        OMPSharedClause* sharedClause = nullptr;
        for (unsigned int i = 0; i < ompDir->getNumClauses(); i++) {
            if (OMPFirstprivateClause* varList = dyn_cast<OMPFirstprivateClause>(ompDir->getClause(i))) {
                firstPrivateClause = varList;
            }
            if (OMPSharedClause* varList = dyn_cast<OMPSharedClause>(ompDir->getClause(i))) {
                sharedClause = varList;
            }
        }

        if (firstPrivateClause == nullptr) {
            if (!isInFirstPrivate)
                AddMutation(ompDir->getSourceRange(), OMPMutationType::FIRSTPRIVATE, 
                    sharedClause ? OMPMutationPos::REPLACE : OMPMutationPos::INSERTAFTER);
        }
        else {
            isInFirstPrivate = true;
        }
    }
}

void FunctionAnalysisVisitor::ProcessSchedule(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process schedule 2");
    if (isa<OMPForDirective>(ompDir) || 
        isa<OMPParallelForDirective>(ompDir)) {
        const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
        const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
        if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
            return;

        unsigned int forLoopCount = varDeclVisitor->GetNestedLoopLevel(capturedStmt, true, false);

        if (forLoopCount >= 1) {
            AddMutation(ompDir->getSourceRange(), OMPMutationType::SCHE_DYN, OMPMutationPos::INSERTAFTER);
            AddMutation(ompDir->getSourceRange(), OMPMutationType::SCHE_GUIDED, OMPMutationPos::INSERTAFTER);
        }  
    }        
}

void FunctionAnalysisVisitor::ProcessProcBind(const OMPExecutableDirective* ompDir) {
    PRINT_DEBUG_MESSAGE("process proc bind");
    if (isa<OMPParallelDirective>(ompDir) || 
        isa<OMPParallelForDirective>(ompDir)) {
        const CapturedStmt* innermostStmt = ompDir->getInnermostCapturedStmt();
        const Stmt* capturedStmt = innermostStmt->getCapturedStmt();
        if (forbiddenStmtVisitor->hasForbiddenStmt(false, false, capturedStmt))
            return;

        AddMutation(ompDir->getSourceRange(), OMPMutationType::PROC_BIND_CLOSE, OMPMutationPos::INSERTAFTER);
        AddMutation(ompDir->getSourceRange(), OMPMutationType::PROC_BIND_SPREAD, OMPMutationPos::INSERTAFTER);
    }        
}


void FunctionAnalysisVisitor::ProcessStatement(BasicBlockInfo* info, CompoundStmt::const_body_iterator stmtIt, CompoundStmtIter* stmtIter) {
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

        std::string statementText;
        raw_string_ostream wrap(statementText);
        st->printPretty(wrap, NULL, PrintingPolicy(astContext->getLangOpts()));
        std::istringstream inputStr(statementText);
        std::string firstline;
        std::getline(inputStr, firstline);
        PRINT_DEBUG_MESSAGE(st->getStmtClassName() << " | first line: " << firstline);

        const Stmt* capturedStmt = ompDir->getInnermostCapturedStmt()->getCapturedStmt();

        PRINT_DEBUG_MESSAGE("capture source range:");
        PrintSourceRange(capturedStmt->getSourceRange(), astContext);

        bool prevIsInFirstPrivate = isInFirstPrivate;
        bool prevIsInSimd = isInSimd;

        if (isa<OMPSimdDirective>(st))
            isInSimd = true;

        // if it is a parallel for and has >=2 nested loops, add it to mutation
        if (!isInSimd)
            ProcessTiling(ompDir);
        ProcessCollapse(ompDir);
        ProcessFirstPrivate(ompDir);
        ProcessSIMD(ompDir);
        ProcessSchedule(ompDir);
        ProcessProcBind(ompDir);

        BasicBlockInfo ompBB;
        ompBB.blockType = BLOCK_TYPE_OMP;
        SourceRange topRange(st->getBeginLoc(), capturedStmt->getEndLoc());
        ompBB.range = topRange;

        BasicBlockInfo dirBB;
        dirBB.blockType = BLOCK_TYPE_OMP_DIR;
        dirBB.range = st->getSourceRange();
        ompBB.blocks.push_back(dirBB);

        dirBB.statements.push_back(CreateStatementInfo(ompDir));

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
        isInFirstPrivate = prevIsInFirstPrivate;
        isInSimd = prevIsInSimd;
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

        ProcessSIMD(forStmt);
        if (!isInSimd)
            ProcessTiling(forStmt);

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
        info->blocks.push_back(topBB);
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

bool FunctionAnalysisVisitor::VisitFunctionDecl(FunctionDecl* func) {
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
    }
    return true;
}

/* OutputFunctionInfo() function collapsed */

void FunctionAnalysisVisitor::OutputMutationList() {
    ofstream funcJsonFile;
    SourceManager& sm = astContext->getSourceManager();
    std::string tmpDir = g_pluginRoot + "/workspace/func_analysis/";
    std::string jsonFileName = tmpDir + g_dirName + ".json";
    funcJsonFile.open(jsonFileName);
    if (funcJsonFile.is_open()) {
        json funcJson;
        for (unsigned int i = 0; i < ompMutations.size(); i++) {
            json item;
            item["index"] = i;
            item["type"] = (unsigned int)ompMutations[i].type;
            item["pos"] = (unsigned int)ompMutations[i].pos;
            item["range_start"] = sm.getFileOffset(sm.getFileLoc(ompMutations[i].range.getBegin()));
            item["range_end"] = sm.getFileOffset(sm.getFileLoc(ompMutations[i].range.getEnd()));
            item["range_start_text"] = ompMutations[i].range.getBegin().printToString(astContext->getSourceManager());
            item["range_end_text"] = ompMutations[i].range.getEnd().printToString(astContext->getSourceManager());
            item["enabled"] = false; 
            item["id_str"] = std::to_string((unsigned int)GetCatFromType(ompMutations[i].type)) + "_" + std::to_string((unsigned int)ompMutations[i].pos) + "_"
                            + std::to_string(sm.getFileOffset(sm.getFileLoc(ompMutations[i].range.getBegin()))) + "_"
                            + std::to_string(sm.getFileOffset(sm.getFileLoc(ompMutations[i].range.getEnd())));
            funcJson["list"].push_back(item);
        }
        funcJsonFile << std::setw(4) << funcJson;
        funcJsonFile << std::endl;
        funcJsonFile.close();
        PRINT_DEBUG_MESSAGE("openmp mutation list - file written - " + jsonFileName);
    }
    else {
        PRINT_DEBUG_MESSAGE("error - file not created - " << jsonFileName << " - " << strerror(errno));
    }
}

void FunctionAnalysisASTConsumer::HandleTranslationUnit(ASTContext &Context) {
    // insert macro at the beginning of the file
    FileID id = rewriter.getSourceMgr().getMainFileID();    
    string originalFilename = rewriter.getSourceMgr().getFilename(rewriter.getSourceMgr().getLocForStartOfFile(id)).str();
    // write original file info to file
    string marker = "";

    std::string basefilename = basename(originalFilename);
    string filename = g_pluginRoot + "/workspace/original_files/" + basefilename;
    visitor->TraverseDecl(Context.getTranslationUnitDecl());
    visitor->OutputMutationList();
}

unique_ptr<ASTConsumer> PluginFunctionAnalysisAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
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

    return make_unique<FunctionAnalysisASTConsumer>(&CI);
}
 
bool PluginFunctionAnalysisAction::ParseArgs(const CompilerInstance &CI, const vector<string> &args) {
    for (unsigned int i = 0, e = args.size(); i != e; i++) {
        if (args[i] == "-limited") {
            g_limitedMode = true;
        }
    }
    return true;
}
