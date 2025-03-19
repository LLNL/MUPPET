#pragma once

using namespace std;
using namespace clang;
using namespace llvm;

extern string g_mainFilename;
extern string g_dirName;
extern string g_pluginRoot;
extern Rewriter rewriter;

#define VARDECL_READ 1
#define VARDECL_WRITE 2
#define VARDECL_DECL 3

class TransformMutationsVisitor : public RecursiveASTVisitor<TransformMutationsVisitor> {
    friend class FindVarDeclVisitor;
private:
    ASTContext *astContext; // used for getting additional AST info
    FindVarDeclVisitor* varDeclVisitor;
    ForbiddenStmtVisitor* forbiddenStmtVisitor;

    std::map<std::string, FunctionInfo*> funcInfos;
    StatementInfo* traversingSingleStatement = NULL;

    const Stmt* anchorPointForRHS = NULL;
    bool sourceChanged = false;

    std::vector<OMPMutation> ompMutations;

    bool isInsideSIMD = false;
    bool isInsideTile = false;
    bool isInParallelLoop = false;
    bool isInFirstPrivate = false;
    bool isInsideCollapse = false;

    OMPMutation* IsPosEnabledForTransform(const Stmt* st, OMPMutationType type, OMPMutationPos pos);
    OMPMutation* IsPosEnabledForTransform(const OMPClause* clause, OMPMutationType type, OMPMutationPos pos);
    StatementInfo CreateStatementInfo(const Stmt* st);

    void ProcessStatement(BasicBlockInfo* info, CompoundStmt::const_body_iterator stmtIt, CompoundStmtIter* stmtIter);
    void ProcessEndOfBasicBlock(BasicBlockInfo* info, CompoundStmtIter* iter);
    void ProcessCompoundStatement(BasicBlockInfo* parentInfo, BasicBlockInfo& basicBlock, const CompoundStmt* compoundStmt);

    void OutputFunctionInfo(FunctionInfo* info, json& j);
    unsigned OutputBasicBlockInfo(BasicBlockInfo* info, json& j);
    unsigned OutputStatementInfo(StatementInfo* info, json& j);

    void ProcessCollapse(const OMPExecutableDirective* ompDir);
    void ProcessTiling(const OMPExecutableDirective* ompDir);
    void ProcessTiling(const ForStmt* forStmt);
    void ProcessSIMD(const ForStmt* forStmt);
    void ProcessSIMD(const OMPExecutableDirective* ompDir);
    void ProcessFirstPrivate(const OMPExecutableDirective* ompDir);
    void ProcessTarget(const OMPExecutableDirective* ompDir);
    void ProcessSchedule(const OMPExecutableDirective* ompDir);
    void ProcessProcBind(const OMPExecutableDirective* ompDir);

    unsigned int forNestLevel = 0;
    SourceLocation fileStart;

public:
    explicit TransformMutationsVisitor(CompilerInstance *CI)
        : astContext(&(CI->getASTContext())) // initialize private members
        , varDeclVisitor(new FindVarDeclVisitor(CI))
        , forbiddenStmtVisitor(new ForbiddenStmtVisitor(CI))
    {
        rewriter.setSourceMgr(astContext->getSourceManager(),
            astContext->getLangOpts());     
        funcInfos.clear();
    }
 
    virtual bool VisitFunctionDecl(FunctionDecl* func);
    virtual bool VisitStmt(Stmt* st);
    virtual void ImportOMPMutations();

    void SetupFileStart();
    bool IsSourceChanged() { return sourceChanged; }
};

class TransformMutationsASTConsumer : public ASTConsumer {
private:
    TransformMutationsVisitor *visitor; // doesn't have to be private

public:
    explicit TransformMutationsASTConsumer(CompilerInstance *CI)
        : visitor(new TransformMutationsVisitor(CI)) // initialize the visitor
        { }
 
    virtual void HandleTranslationUnit(ASTContext &Context);
};

class PluginTransformMutationsAction : public PluginASTAction {
protected:
    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file); 
    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args);
};
