#include <iostream>
#include <string>
#include <unistd.h>

#include "utils.h"

extern Rewriter rewriter;

std::string extendPrecisionFuncName;
std::string castBackFuncName;
std::string extendPrecisionTypeName;
bool useExtendedPrecision = false;
bool searchInHostFunctions = true;
bool searchInHeaders = false;
bool pureHostFunctions = false;
bool subExpressionIsolation = false;

vector<SubExpressionIsolationInfo> subExpressionIsolationInfos;

void PrintSourceLocation(SourceLocation loc, ASTContext *astContext)
{
    std::string text = loc.printToString(astContext->getSourceManager());
    PRINT_DEBUG_MESSAGE("location: " << text);
}

void PrintSourceRange(SourceRange range, ASTContext *astContext)
{
    std::string text1 = range.getBegin().printToString(astContext->getSourceManager());
    std::string text2 = range.getEnd().printToString(astContext->getSourceManager());
    PRINT_DEBUG_MESSAGE("\toffset: " << text1 << ", " << text2);
}

void PrintSourceRangeToFile(SourceRange range, ASTContext *astContext, ostream &out)
{
    std::string text1 = range.getBegin().printToString(astContext->getSourceManager());
    std::string text2 = range.getEnd().printToString(astContext->getSourceManager());
    out << "offending text: " << text1 << ", " << text2 << std::endl;
}

void PrintStatement(string prefix, const Stmt *st, ASTContext *astContext)
{
    if (st == NULL) {
        PRINT_DEBUG_MESSAGE(prefix << "None");
        return;
    }
    std::string statementText;
    raw_string_ostream wrap(statementText);
    st->printPretty(wrap, NULL, PrintingPolicy(astContext->getLangOpts()));
    PRINT_DEBUG_MESSAGE(prefix << st->getStmtClassName() << ", " << statementText);
    PrintSourceRange(st->getSourceRange(), astContext);
}

void PrintStatementToFile(string prefix, const Stmt *st, ASTContext *astContext, ostream &out)
{
    std::string statementText;
    raw_string_ostream wrap(statementText);
    st->printPretty(wrap, NULL, PrintingPolicy(astContext->getLangOpts()));
    out << prefix << st->getStmtClassName() << ", " << statementText << std::endl;
    PrintSourceRangeToFile(st->getSourceRange(), astContext, out);
}

bool IsComplexStatement(const Stmt *st)
{
    return (isa<CompoundStmt>(st)) ||
           (isa<ForStmt>(st)) ||
           (isa<DoStmt>(st)) ||
           (isa<WhileStmt>(st)) ||
           (isa<SwitchStmt>(st)) ||
           (isa<IfStmt>(st)) ||
           (isa<OMPExecutableDirective>(st));
}

const Stmt *FindLHSRHS(Stmt *st, const Stmt *anchor, const Expr **lhsPtr, ASTContext *astContext)
{
    if (anchor == NULL)
    {
        // find writes: use operators
        // CompoundAssignOperator (for *=, +=, etc.)
        // BinaryOperator (for =)
        // CXXOperatorCallExpr (for overloads)
        if (const BinaryOperator *binaryOp = dyn_cast<BinaryOperator>(st))
        {
            if (binaryOp->isAssignmentOp())
            {
                Expr *lhs = binaryOp->getLHS();
                Expr *rhs = binaryOp->getRHS();

                //PrintStatement("\t\tLHS: ", lhs, astContext);
                //PrintStatement("\t\tRHS: ", rhs, astContext);

                if (lhsPtr)
                {
                    *lhsPtr = lhs;
                }

                return rhs;
            }
        }
        else if (const CXXOperatorCallExpr *opOverload = dyn_cast<CXXOperatorCallExpr>(st))
        {
            // Cheat depending on the fact that all equal overloaded operators are packed together
            if (opOverload->isAssignmentOp())
            {
                for (unsigned int i = 0; i < opOverload->getNumArgs(); i++)
                {
                    PrintStatement("\t\targ: ", opOverload->getArg(i), astContext);
                }

                if (lhsPtr)
                {
                    *lhsPtr = opOverload->getArg(0);
                }

                return opOverload->getArg(1);
            }
        }
        return anchor;
    }
    else if (anchor == st)
        return NULL;
    else
        return anchor;
}

string basename(std::string path)
{
    return std::string(std::find_if(path.rbegin(), path.rend(), MatchPathSeparator()).base(), path.end());
}

// find compound assignment

void FindCompoundAssignment(const Stmt *st, const Expr **lhsPtr, const Expr **rhsPtr) {
    if (!st)
        return;
    const Expr *lhs = NULL;
    const Expr *rhs = NULL;
    if (const BinaryOperator *binaryOp = dyn_cast<BinaryOperator>(st))
    {
        if (binaryOp->isCompoundAssignmentOp())
        {
            lhs = binaryOp->getLHS();
            rhs = binaryOp->getRHS();
        }
    }
    else if (const CXXOperatorCallExpr *opOverload = dyn_cast<CXXOperatorCallExpr>(st))
    {
        if (opOverload->isAssignmentOp() && opOverload->getOperator() != OO_Equal)
        {
            lhs = opOverload->getArg(0);
            rhs = opOverload->getArg(1);
        }
    }

    if (lhsPtr)
        *lhsPtr = lhs;
    if (rhsPtr)
        *rhsPtr = rhs;
}

void FindRegularAssignment(const Stmt* st, const Expr** lhsPtr, const Expr** rhsPtr) {
    const Expr *lhs = NULL;
    const Expr *rhs = NULL;
    if (const BinaryOperator *binaryOp = dyn_cast<BinaryOperator>(st))
    {
        if (binaryOp->isAssignmentOp())
        {
            lhs = binaryOp->getLHS();
            rhs = binaryOp->getRHS();
        }
    }
    else if (const CXXOperatorCallExpr *opOverload = dyn_cast<CXXOperatorCallExpr>(st))
    {
        if (opOverload->isAssignmentOp() && opOverload->getOperator() == OO_Equal)
        {
            lhs = opOverload->getArg(0);
            rhs = opOverload->getArg(1);
        }
    }

    if (lhsPtr)
        *lhsPtr = lhs;
    if (rhsPtr)
        *rhsPtr = rhs;
}

/// 'Loc' is the end of a statement range. This returns the location
/// immediately after the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findLocationAfterSemi(SourceLocation loc,
                                     ASTContext &Ctx, bool IsDecl)
{
    SourceLocation SemiLoc = findSemiAfterLocation(loc, Ctx, IsDecl);
    if (SemiLoc.isInvalid())
        return SourceLocation();
    return SemiLoc.getLocWithOffset(1);
}

/// \arg Loc is the end of a statement range. This returns the location
/// of the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findSemiAfterLocation(SourceLocation loc,
                                     ASTContext &Ctx,
                                     bool IsDecl)
{
    SourceManager &SM = Ctx.getSourceManager();
    if (loc.isMacroID())
    {
        if (!Lexer::isAtEndOfMacroExpansion(loc, SM, Ctx.getLangOpts(), &loc))
            return SourceLocation();
    }
    loc = Lexer::getLocForEndOfToken(loc, /*Offset=*/0, SM, Ctx.getLangOpts());

    // Break down the source location.
    std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

    // Try to load the file buffer.
    bool invalidTemp = false;
    StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
    if (invalidTemp)
        return SourceLocation();

    const char *tokenBegin = file.data() + locInfo.second;

    // Lex from the start of the given location.
    Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
                Ctx.getLangOpts(),
                file.begin(), tokenBegin, file.end());
    Token tok;
    lexer.LexFromRawLexer(tok);
    if (tok.isNot(tok::semi))
    {
        if (!IsDecl)
            return SourceLocation();
        // Declaration may be followed with other tokens; such as an __attribute,
        // before ending with a semicolon.
        return findSemiAfterLocation(tok.getLocation(), Ctx, /*IsDecl*/ true);
    }

    return tok.getLocation();
}

SourceRange FindVarDeclScope(const VarDecl *varDecl, ASTContext *astContext)
{
    const Stmt* currentStmt = varDecl->getInit() ? dyn_cast<Stmt>(varDecl->getInit()) : NULL;
    const Decl* currentDecl = varDecl->getInit() ? NULL : varDecl;
    // if true, use stmt, else use decl
    bool useStmtOrDecl = currentStmt != NULL;
    while (true)
    {
        const auto &parents = useStmtOrDecl ? astContext->getParents(*currentStmt) : astContext->getParents(*currentDecl);

        if (parents.empty()) {
            PRINT_DEBUG_MESSAGE("empty parents");
            return SourceRange();
        }

        const Stmt *parentStmt = parents[0].get<Stmt>();
        const Decl *parentDecl = parents[0].get<Decl>();
        if (parentStmt == NULL && parentDecl == NULL) {
            PRINT_DEBUG_MESSAGE("neither stmt or decl");
            return SourceRange();
        }
        else if (parentStmt) {
            if (const CompoundStmt *compoundStmt = dyn_cast<CompoundStmt>(parentStmt))
            {
                return compoundStmt->getSourceRange();
            }            
            useStmtOrDecl = true;
        }
        else {
            useStmtOrDecl = false;
        }

        currentStmt = parentStmt;
        currentDecl = parentDecl;
    }
}

SourceLocation GetExpandedLocation(SourceLocation orig, bool isBeginOrEndLoc) {
    if (orig.isMacroID()) {
        PRINT_DEBUG_MESSAGE("\t\texpanding source range");
        CharSourceRange expandedRange = rewriter.getSourceMgr().getImmediateExpansionRange(orig);

        if (isBeginOrEndLoc)
            return expandedRange.getBegin();
        else
            return expandedRange.getEnd();
    }
    return orig;
}

SourceRange GetExpandedRange(SourceRange orig) {
    SourceLocation beginLoc = GetExpandedLocation(orig.getBegin(), true);
    SourceLocation endLoc = GetExpandedLocation(orig.getEnd(), false);
    return SourceRange(beginLoc, endLoc);
}

void SetupTransformIdentifiers(bool isHost)
 {
    /*if (isHost) {
        if (useExtendedPrecision) {
            extendPrecisionFuncName = host_extendPrecisionFuncName_Extended;
            castBackFuncName = host_castBackFuncName_Extended;
            extendPrecisionTypeName = host_extendPrecisionTypeName_Extended;
        }
        else {
            extendPrecisionFuncName = host_extendPrecisionFuncName_NonExtended;
            castBackFuncName = host_castBackFuncName_NonExtended;
            extendPrecisionTypeName = host_extendPrecisionTypeName_NonExtended;                
        }
    }
    else {
        if (useExtendedPrecision) {
            extendPrecisionFuncName = device_extendPrecisionFuncName_Extended;
            castBackFuncName = device_castBackFuncName_Extended;
            extendPrecisionTypeName = device_extendPrecisionTypeName_Extended;
        }
        else {
            extendPrecisionFuncName = device_extendPrecisionFuncName_NonExtended;
            castBackFuncName = device_castBackFuncName_NonExtended;
            extendPrecisionTypeName = device_extendPrecisionTypeName_NonExtended;                
        }
    }*/    
}

#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <iomanip>

#include "utils.h"
#include "TransformMutations.h"

bool FindVarDeclVisitor::isArray(const VarDecl* varDecl) {
    clang::QualType type = varDecl->getType();
    const clang::Type* typeObj = type.getTypePtrOrNull();
    if (const clang::ArrayType *arr = dyn_cast<clang::ArrayType>(typeObj)) {
        return true;
    }    
    return false;
}

bool FindVarDeclVisitor::VisitStmt(Stmt* st) {
    rhsAnchor = FindLHSRHS(st, rhsAnchor, nullptr, astContext);

    if (isa<OMPParallelDirective>(st) || isa<OMPForDirective>(st) || 
        isa<OMPParallelForDirective>(st)) {
        OMPExecutableDirective* ompDir = dyn_cast<OMPExecutableDirective>(st);
        OMPFirstprivateClause* firstPrivateClause = nullptr;
        for (unsigned int i = 0; i < ompDir->getNumClauses(); i++) {
            if (OMPFirstprivateClause* varList = dyn_cast<OMPFirstprivateClause>(ompDir->getClause(i))) {
                firstPrivateClause = varList;
            }
        }        
        if (firstPrivateClause) {
            innerFirstPrivate = true;
        }
        innerParallel = true;
    }

    if (DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(st)) {
        ValueDecl* decl = declRefExpr->getDecl();
        if (VarDecl* varDecl = dyn_cast<VarDecl>(decl)) {
            allVarDeclsWithArray[varDecl->getNameAsString()] = varDecl;
            if (!isArray(varDecl)) {
                allVarDecls[varDecl->getNameAsString()] = varDecl;

                if (varDeclProperties.find(varDecl->getNameAsString()) == varDeclProperties.end()) {
                    varDeclProperties[varDecl->getNameAsString()] = 0;
                }

                if (rhsAnchor) {
                    varDeclProperties[varDecl->getNameAsString()] |= VARDECL_WRITE;
                }
                else {
                    varDeclProperties[varDecl->getNameAsString()] |= VARDECL_READ;
                }
            }
        }
    }

    if (const DeclStmt* declStmt = dyn_cast<DeclStmt>(st)) {
        if (declStmt->isSingleDecl()) {
            const Decl* decl = declStmt->getSingleDecl();
            if (isa<VarDecl>(decl)) {
                const VarDecl* varDecl = dyn_cast<VarDecl>(decl);
                if (!isArray(varDecl)) {
                    if (varDeclProperties.find(varDecl->getNameAsString()) == varDeclProperties.end()) {
                        varDeclProperties[varDecl->getNameAsString()] = 0;
                    }                
                    varDeclProperties[varDecl->getNameAsString()] |= VARDECL_DECL;
                }
            }
        }
        else {
            DeclStmt::const_decl_iterator it;
            for (it = declStmt->decl_begin(); it != declStmt->decl_end(); it++) {
                const Decl* decl = *it;
                if (isa<VarDecl>(decl)) {
                    const VarDecl* varDecl = dyn_cast<VarDecl>(decl);
                    if (!isArray(varDecl)) {                    
                        if (varDeclProperties.find(varDecl->getNameAsString()) == varDeclProperties.end()) {
                            varDeclProperties[varDecl->getNameAsString()] = 0;
                        }                
                        varDeclProperties[varDecl->getNameAsString()] |= VARDECL_DECL;                           
                    }             
                }
            }
        }
    }    

    if (CallExpr* callExpr = dyn_cast<CallExpr>(st)) {

    }

    return true;
}

std::set<std::string> FindVarDeclVisitor::getReadOnlyVars() {
    std::set<std::string> ret;
    for (auto x : varDeclProperties) {
        if (x.second == VARDECL_READ) {
            ret.insert(x.first);
        }
    }
    return ret;
}

unsigned int FindVarDeclVisitor::GetNestedLoopLevel(const Stmt* capturedStmt, bool strictCheck, bool withArray) {
    unsigned int forLoopCount = 0;
    const Stmt* currentStmt = capturedStmt;
        
    while (true) {
        if (isa<ForStmt>(currentStmt)) {
            const ForStmt* forStmt = dyn_cast<ForStmt>(currentStmt);

            PrintStatement("Init: ", forStmt->getInit(), astContext);
            PrintStatement("Inc.: ", forStmt->getInc(), astContext);
            PrintStatement("Cond: ", forStmt->getCond(), astContext);

            // there should only be one variable
            clear();
            TraverseStmt((Stmt*)forStmt->getInit());
            TraverseStmt((Expr*)forStmt->getInc());
            TraverseStmt((Expr*)forStmt->getCond());

            if (withArray == false && numVarDecls() > 2) {
                PRINT_DEBUG_MESSAGE("too many VarDecls");
                break;
            }
            if (withArray == true && numVarDeclsWithArray() > 1) {
                PRINT_DEBUG_MESSAGE("has array VarDecls");
                break;
            }

            if (strictCheck) {
                if (forStmt->getInit()) {
                    if (!isa<DeclStmt>(forStmt->getInit())) {
                        canonOpVisitor->SetCanonOpKind(CanonOpKind::ASSIGNMENT);
                        canonOpVisitor->TraverseStmt((Stmt*)forStmt->getInit());
                        if (!canonOpVisitor->HasCanonOp()) {
                            PRINT_DEBUG_MESSAGE("init format incorrect");
                            break;
                        }
                    }
                }
                else {
                    PRINT_DEBUG_MESSAGE("init format incorrect");
                    break;
                }
                if (forStmt->getCond()) {
                    canonOpVisitor->SetCanonOpKind(CanonOpKind::COMPARE);
                    canonOpVisitor->TraverseStmt((Expr*)forStmt->getCond());
                    if (!canonOpVisitor->HasCanonOp()) {
                        PRINT_DEBUG_MESSAGE("cond format incorrect");
                        break;
                    }
                }
                else {
                    PRINT_DEBUG_MESSAGE("cond format incorrect");
                    break;
                }
                if (forStmt->getInc()) {
                    canonOpVisitor->SetCanonOpKind(CanonOpKind::INCREMENT);
                    canonOpVisitor->TraverseStmt((Expr*)forStmt->getInc());
                    if (!canonOpVisitor->HasCanonOp()) {
                        PRINT_DEBUG_MESSAGE("inc format incorrect");
                        break;
                    }
                }
                else {
                    PRINT_DEBUG_MESSAGE("inc format incorrect");
                    break;
                }
            }                   

            forLoopCount++;
            currentStmt = forStmt->getBody();
        }
        else if (isa<CompoundStmt>(currentStmt)) {
            const CompoundStmt* compoundStmt = dyn_cast<CompoundStmt>(currentStmt);
            if (compoundStmt->size() != 1) {
                PRINT_DEBUG_MESSAGE("compoundstmt size incorrect");
                break;
            }
            else {
                currentStmt = compoundStmt->body_front();
            }
        }
        else {
            PRINT_DEBUG_MESSAGE("not a forstmt or compoundstmt");
            break;
        }
    }

    PRINT_DEBUG_MESSAGE("Level of nested loops: " << forLoopCount);
    return forLoopCount;
}

bool ForbiddenStmtVisitor::VisitStmt(Stmt* st) {
    if (isa<BreakStmt>(st) || isa<ContinueStmt>(st) || 
        isa<ReturnStmt>(st) ) {
        PrintStatement("\t\t==> forbidden control flow statement:", st, astContext);
        return false;
    }

    if (isa<OMPCriticalDirective>(st) || isa<OMPBarrierDirective>(st) || 
        isa<OMPMasterDirective>(st) || isa<OMPForDirective>(st)) {
        if (failWhenOMP) {
            PrintStatement("\t\t==> forbidden OMP statement:", st, astContext);
            return false;
        }
    }

    if (isa<OMPForDirective>(st) && failWhenOMPFor) {
        PrintStatement("\t\t==> forbidden OMPFor statement:", st, astContext);
        return false;        
    }

    return true;
}

bool CanonOpVisitor::VisitStmt(Stmt* st) {
    if (kind == CanonOpKind::ASSIGNMENT) {
        if (BinaryOperator* bo = dyn_cast<BinaryOperator>(st)) {
            if (bo->isAssignmentOp())
                hasCanonOp = true;
        }
    }
    else if (kind == CanonOpKind::COMPARE) {
        if (BinaryOperator* bo = dyn_cast<BinaryOperator>(st)) {
            if (bo->isComparisonOp())
                hasCanonOp = true;
        }
    }
    else if (kind == CanonOpKind::INCREMENT) {
        if (BinaryOperator* bo = dyn_cast<BinaryOperator>(st)) {
            BinaryOperatorKind opCode = bo->getOpcode();
            if (opCode == BinaryOperatorKind::BO_Add ||
                opCode == BinaryOperatorKind::BO_Sub ||
                opCode == BinaryOperatorKind::BO_AddAssign ||
                opCode == BinaryOperatorKind::BO_SubAssign)
                hasCanonOp = true;
        }        
        else if (UnaryOperator* uo = dyn_cast<UnaryOperator>(st)) {
            UnaryOperatorKind opCode = uo->getOpcode();
            if (opCode == UnaryOperatorKind::UO_PostInc ||
                opCode == UnaryOperatorKind::UO_PostDec ||
                opCode == UnaryOperatorKind::UO_PreInc ||
                opCode == UnaryOperatorKind::UO_PreDec)
                hasCanonOp = true;
        }
    }
    return true;
}
