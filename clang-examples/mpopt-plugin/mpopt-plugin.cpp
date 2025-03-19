#include "utils.h"
#include "TransformMutations.h"
#include "FunctionAnalysis.h"

Rewriter rewriter;
string g_mainFilename;
string g_dirName;
string g_pluginRoot;
bool g_limitedMode = false;

FrontendPluginRegistry::Add<PluginTransformMutationsAction>
    TransformMutationsAction("mpopt-trans-mutations", "pLiner for GPU: analyzing functions in source code");
FrontendPluginRegistry::Add<PluginFunctionAnalysisAction>
    FunctionAnalysisAction("mpopt-function-analysis", "pLiner for GPU: transform and mutate code");