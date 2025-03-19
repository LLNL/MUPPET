#pragma once

#include <iostream>
#include <fstream>

#include "clang/Driver/Options.h"
#include "clang/AST/Decl.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include "json.hpp"

using json = nlohmann::json;

using namespace std;
using namespace clang;
using namespace llvm;

extern string g_mainFilename;
extern string g_dirName;
extern string g_pluginRoot;
extern Rewriter rewriter;

#define endline "\n"

#define VERBOSE 1
#define PRINT_DEBUG_MESSAGE(s) if (VERBOSE > 0) {errs() << s << endline; }
#define PRINT_DETAILED_DEBUG_MESSAGE(s) if (VERBOSE > 1) {errs() << s << endline; }

struct FunctionInfo;
struct BasicBlockInfo;
struct LoopInfo;
struct FunctionInfo;

struct StatementInfo {
    const Stmt* stmt;
    SourceRange range;
    vector<const DeclRefExpr*> reads;
    vector<const DeclRefExpr*> writes;
    vector<const VarDecl*> decls;
    vector<const VarDecl*> definitions;
    vector<const CallExpr*> calls;
    vector<const Expr*> consts;
    
    // a shortcut for the function call graph
    vector<FunctionInfo*> callLinks;

    // only used during raising precision: store the json file here
    json stmtElement;  
    unsigned int enabled;  
};

#define BLOCK_TYPE_BASIC 0          // basic
#define BLOCK_TYPE_LOOP 1           // for, while, do while
#define BLOCK_TYPE_LOOP_COMPONENT 2 // loop cond + body
#define BLOCK_TYPE_COND 3           // switch case, if
#define BLOCK_TYPE_COND_COMPONENT 4 // if/switch cond + body 
#define BLOCK_TYPE_OMP 5            // omp whole block
#define BLOCK_TYPE_OMP_DIR 6        // omp directive
#define BLOCK_TYPE_FUNC 7           // function body

#define END_OF_BLOCK_FALSE 0
#define END_OF_BLOCK_NORMAL 1
#define END_OF_BLOCK_FLAT_COMPOUND 2

struct CompoundStmtIter {
    const CompoundStmt* stmt;
    CompoundStmt::const_body_iterator basicBlockHead;
    CompoundStmt::const_body_iterator basicBlockTail;    
};

struct BasicBlockInfo {
    SourceRange range;
    unsigned int blockType;
    unsigned int enabled;
    // a basic block has either other blocks or statements.
    // do not allow a mixture of the two.
    vector<BasicBlockInfo> blocks;
    vector<StatementInfo> statements;
    // store the corresponding compoundStmt (or none) for easy insertion of exit blocks
    const Stmt* compoundStmt;
};

struct FunctionInfo : BasicBlockInfo {
    string name;
    // a shortcut for the function call graph
    vector<FunctionInfo*> calls;
};

struct RegionInRange {
    SourceRange range;
    // if true, range.end is not "beginning of next basic block", but "end of this block". Treated differently during insertion of exit processing code.
    int endOfBlock;
    // if true, this region consists of one whole compound statement for/while/if/switch. insertion point for entry/exit processing should be different.
    bool compoundBlock;
};

enum OMPMutationPos {
    INSERTBEFORE,
    INSERTAFTER,
    REPLACE
};

enum OMPMutationType {
    COLLAPSE,
    SIMD,
    FIRSTPRIVATE,
    TILE_8,
    TILE_16,
    TILE_32,    
    SCHE_DYN,
    SCHE_GUIDED,
    PROC_BIND_CLOSE,
    PROC_BIND_SPREAD,
};

enum OMPMutationCat {
    CAT_COLLAPSE,
    CAT_SIMD,
    CAT_FIRSTPRIVATE,
    CAT_TILE,
    CAT_SCHE,
    CAT_PROC,
};

inline OMPMutationCat GetCatFromType(OMPMutationType type) {
    if ((int)type < (int)OMPMutationType::TILE_8) {
        return (OMPMutationCat)(int)type;
    }
    else if ((int)type < (int)OMPMutationType::SCHE_DYN) {
        return OMPMutationCat::CAT_TILE;
    }
    else if ((int)type < (int)OMPMutationType::PROC_BIND_CLOSE) {
        return OMPMutationCat::CAT_SCHE;
    }
    else {
        return OMPMutationCat::CAT_PROC;
    }
}

struct OMPMutation {
    SourceRange range;
    OMPMutationType type;
    OMPMutationPos pos;
    unsigned int rangeStart, rangeEnd;
};

// Used by std::find_if
struct MatchPathSeparator
{
    bool operator()(char ch) const {
        return ch == '/';
    }
};

// Function to get the base name of the file provided by path
string basename(std::string path);

static const char* sSystemIncludeDirectories[] = {
    "/usr/lib/gcc",
    "/usr/local/cuda",
    "/lib/clang/",
    "/usr/include/",
    "/usr/bin/",
};

const map<std::string, std::string> mathcalls_base = {
    // TODO: expand based on CUDA Math API. Currently only for functions used in the test cases.
    {"acos", "acosl"},
    {"log", "logl"},
    {"asin", "asinl"},
    {"atan2", "atan2l"},
    {"ceil", "ceill"},
    {"exp", "expl"},
    {"pow", "powl"},
    {"atan", "atanl"},
    {"sqrt", "sqrtl"},
    {"fabs", "fabsl"},
    {"sin", "sinl"},
    {"fmod", "fmodl"},
    // from Varity
    {"sinh", "sinhl"},
    {"cosh", "coshl"},
    {"tanh", "tanhl"},
    {"log10", "log10l"},
    {"floor", "floorl"},
    {"ldexp", "ldexpl"},
    {"cos", "cosl"},
    {"fmax", "fmaxl"},
    {"fmin", "fminl"},
    {"copysign", "copysignl"}
};

void PrintStatement(string prefix, const Stmt* st, ASTContext* astContext);
void PrintStatementToFile(string prefix, const Stmt *st, ASTContext *astContext, ostream &out);
void PrintSourceLocation(SourceLocation loc, ASTContext* astContext);
void PrintSourceRange(SourceRange range, ASTContext* astContext);
void PrintSourceRangeToFile(SourceRange range, ASTContext* astContext, ostream& out);

bool IsComplexStatement(const Stmt* st);
const Stmt* FindLHSRHS(Stmt* st, const Stmt* anchor, const Expr** lhsPtr, ASTContext* astContext);
void FindCompoundAssignment(const Stmt* st, const Expr** lhsPtr, const Expr** rhsPtr);
void FindRegularAssignment(const Stmt* st, const Expr** lhsPtr, const Expr** rhsPtr);

// lifted from clang source code (TODO: licensing?)
SourceLocation findLocationAfterSemi(SourceLocation loc, ASTContext &Ctx, bool IsDecl);
SourceLocation findSemiAfterLocation(SourceLocation loc, ASTContext &Ctx, bool IsDecl);
SourceRange FindVarDeclScope(const VarDecl *varDecl, ASTContext *astContext);

template<class T>
bool IsInsideStmt(const Stmt* st, ASTContext* astContext) {
    const Stmt* currentStmt = st;
    while (true) {
        const auto& parents = astContext->getParents(*currentStmt);
        if (parents.empty()) {
            return false; // end of traversal
        }

        const Stmt* parentStmt = parents[0].get<Stmt>();
        if (parentStmt == NULL) {
            return false;
        }
        const T* typedStmt = dyn_cast<T>(parentStmt);
        if (typedStmt) {
            return true;
        }

        currentStmt = parentStmt;
    }
}

inline std::string GetTransformedTypeName(std::string typeName, unsigned int isVector) {
    string vectorSuffix = isVector == 0 ? "" : std::to_string(isVector + 1);
    string newTypeName = typeName + vectorSuffix;
    return newTypeName;
}

inline std::string GetTransformedVarName(std::string varName, int regionIndex = -1) {
    if (regionIndex == -1)
        return varName + "_d";
    else
        return varName + "_d" + to_string(regionIndex);
}

inline bool ends_with(std::string const & value, std::string const & ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

SourceLocation GetExpandedLocation(SourceLocation orig, bool isBeginOrEndLoc);
SourceRange GetExpandedRange(SourceRange orig);

struct SubExpressionIsolationInfo {
    string funcName;
    vector<int> stmtIndices;
    vector<int> currentSubs;
    vector<int> subExpressionTree;
    vector<Stmt*> subExpressions;
    vector<int> subExTypes;

    SourceRange range;
};

extern vector<SubExpressionIsolationInfo> subExpressionIsolationInfos;

class FindVarDeclVisitor;

enum CanonOpKind {
    ASSIGNMENT,
    COMPARE,
    INCREMENT
};

class CanonOpVisitor : public RecursiveASTVisitor<CanonOpVisitor> {
    friend class FindVarDeclVisitor;
    ASTContext *astContext; // used for getting additional AST info
    CanonOpKind kind;
    bool hasCanonOp = false;
public:
    explicit CanonOpVisitor(CompilerInstance *CI)
        : astContext(&(CI->getASTContext())) // initialize private members
    {
        rewriter.setSourceMgr(astContext->getSourceManager(),
            astContext->getLangOpts());     
    }   

    void SetCanonOpKind(CanonOpKind kind) { hasCanonOp = false; this->kind = kind; }
    bool HasCanonOp() {return hasCanonOp; }
    virtual bool VisitStmt(Stmt* st);    
};

class FindVarDeclVisitor : public RecursiveASTVisitor<FindVarDeclVisitor> {
    friend class TransformMutationsVisitor;

    ASTContext *astContext; // used for getting additional AST info
    CanonOpVisitor* canonOpVisitor;
    std::map<std::string, VarDecl*> allVarDecls;
    std::map<std::string, VarDecl*> allVarDeclsWithArray;
    std::map<std::string, int> varDeclProperties;

    const Stmt* rhsAnchor = nullptr;
    bool innerFirstPrivate = false;
    bool innerParallel = false;

    bool isArray(const VarDecl* varDecl);
public:
    explicit FindVarDeclVisitor(CompilerInstance *CI)
        : astContext(&(CI->getASTContext())) // initialize private members
        , canonOpVisitor(new CanonOpVisitor(CI))
    {
        rewriter.setSourceMgr(astContext->getSourceManager(),
            astContext->getLangOpts());     
    }

    virtual bool VisitStmt(Stmt* st);
    unsigned int numVarDecls() { return allVarDecls.size(); }
    unsigned int numVarDeclsWithArray() { return allVarDeclsWithArray.size(); }

    std::set<std::string> getReadOnlyVars();
    bool hasInnerFirstPrivate() { return innerFirstPrivate; }
    bool hasInnerParallel() { return innerParallel; }
    void clear() { 
        allVarDecls.clear();
        allVarDeclsWithArray.clear();
        varDeclProperties.clear();
        innerFirstPrivate = false;
        innerParallel = false;
    }

    unsigned int GetNestedLoopLevel(const Stmt* capturedStmt, bool strictCheck=true, bool withArray=false);
};

class ForbiddenStmtVisitor : public RecursiveASTVisitor<ForbiddenStmtVisitor> {
    friend class TransformMutationsVisitor;    
    ASTContext *astContext; // used for getting additional AST info
    bool failWhenOMP = true;
    bool failWhenOMPFor = false;
public:
    explicit ForbiddenStmtVisitor(CompilerInstance *CI)
        : astContext(&(CI->getASTContext())) // initialize private members
    {
        rewriter.setSourceMgr(astContext->getSourceManager(),
            astContext->getLangOpts());     
    }    

    void setFailWhenOMP(bool val) { failWhenOMP = val; }
    void setFailWhenOMPFor(bool val) { failWhenOMPFor = val; }
    bool hasForbiddenStmt(bool failIfOMP, bool failIfOMPFor, const Stmt* st) {
        setFailWhenOMP(failIfOMP);
        setFailWhenOMPFor(failIfOMPFor);
        if (!TraverseStmt((Stmt*)st)) {
            return true;
        }
        return false;
    }
    virtual bool VisitStmt(Stmt* st);
};