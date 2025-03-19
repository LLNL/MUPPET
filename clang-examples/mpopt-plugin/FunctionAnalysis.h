#pragma once

using namespace std;
using namespace clang;
using namespace llvm;

extern string g_mainFilename;
extern string g_dirName;
extern bool g_limitedMode;
extern string g_pluginRoot;
extern Rewriter rewriter;

class FunctionAnalysisVisitor : public RecursiveASTVisitor<FunctionAnalysisVisitor> {
private:
    ASTContext *astContext; // used for getting additional AST info
    FindVarDeclVisitor* varDeclVisitor;
    ForbiddenStmtVisitor* forbiddenStmtVisitor;    
    
    std::map<std::string, FunctionInfo*> funcInfos;
    StatementInfo* traversingSingleStatement = NULL;

    bool isInFirstPrivate = false;
    bool isInSimd = false;
    OMPExecutableDirective* topParallel = nullptr;
    const Stmt* anchorPointForRHS = NULL;

    std::vector<OMPMutation> ompMutations;

    StatementInfo CreateStatementInfo(const Stmt* st);

    void ProcessStatement(BasicBlockInfo* info, CompoundStmt::const_body_iterator stmtIt, CompoundStmtIter* stmtIter);
    void ProcessEndOfBasicBlock(BasicBlockInfo* info, CompoundStmtIter* iter);
    void ProcessCompoundStatement(BasicBlockInfo* parentInfo, BasicBlockInfo& basicBlock, const CompoundStmt* compoundStmt);

    void OutputFunctionInfo(FunctionInfo* info, json& j);
    unsigned OutputBasicBlockInfo(BasicBlockInfo* info, json& j);
    unsigned OutputStatementInfo(StatementInfo* info, json& j);

    unsigned int GetNestedLoopLevel(const Stmt* capturedStmt, bool withArray=false);

    void ProcessCollapse(const OMPExecutableDirective* ompDir);
    void ProcessTiling(const OMPExecutableDirective* ompDir);
    void ProcessTiling(const ForStmt* forStmt);
    void ProcessSIMD(const ForStmt* forStmt);
    void ProcessSIMD(const OMPExecutableDirective* ompDir);
    void ProcessFirstPrivate(const OMPExecutableDirective* ompDir);
    void ProcessTarget(const OMPExecutableDirective* ompDir);
    void ProcessSchedule(const OMPExecutableDirective* ompDir);
    void ProcessProcBind(const OMPExecutableDirective* ompDir);

    void AddMutation(SourceRange range, OMPMutationType type, OMPMutationPos pos);

public:
    explicit FunctionAnalysisVisitor(CompilerInstance *CI)
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

    virtual void OutputMutationList();
};

class FunctionAnalysisASTConsumer : public ASTConsumer {
private:
    FunctionAnalysisVisitor *visitor; // doesn't have to be private

public:
    explicit FunctionAnalysisASTConsumer(CompilerInstance *CI)
        : visitor(new FunctionAnalysisVisitor(CI)) // initialize the visitor
        { }
 
    virtual void HandleTranslationUnit(ASTContext &Context);
};

class PluginFunctionAnalysisAction : public PluginASTAction {
protected:
    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file); 
    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args);
    bool limitedMode = false;
};
