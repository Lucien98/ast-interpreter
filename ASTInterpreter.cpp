//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : 
   public EvaluatedExprVisitor<InterpreterVisitor> {
public:
   explicit InterpreterVisitor(const ASTContext &context, Environment * env)
   : EvaluatedExprVisitor(context), mEnv(env) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator (BinaryOperator * bop) {
       if(mEnv->hasReturn()) return;
	   VisitStmt(bop);
	   mEnv->binop(bop);
   }
   virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
       if(mEnv->hasReturn()) return;
       VisitStmt(expr);
	   mEnv->declref(expr);
   }
   virtual void VisitCallExpr(CallExpr * call) {
       if(mEnv->hasReturn()) return;
	   VisitStmt(call);
	   mEnv->call(call, 0);
       std::string sys_fun("GET, PRINT, MALLOC, FREE");
       if(FunctionDecl *funcdecl = call->getDirectCallee()){
           if(sys_fun.find(funcdecl->getName())==std::string::npos){
               VisitStmt(funcdecl->getBody());
               mEnv->call(call, 1);
           }
       }
   }
   virtual void VisitDeclStmt(DeclStmt * declstmt) {
       if(mEnv->hasReturn()) return;
	   mEnv->decl(declstmt);
   }
   virtual void VisitIfStmt(IfStmt * ifstmt){
       if(mEnv->hasReturn()) return;
       mEnv->expr(ifstmt->getCond())?Visit(ifstmt->getThen()):\
           ifstmt->getElse()? Visit(ifstmt->getElse()):(void)0;
   }
   virtual void VisitWhileStmt(WhileStmt * whilestmt){
       if(mEnv->hasReturn()) return;
       while(mEnv->expr(whilestmt->getCond())){
           VisitStmt(whilestmt->getBody());
       }
   }
   virtual void VisitForStmt(ForStmt * forstmt){
       if(mEnv->hasReturn()) return;
       for(forstmt->getInit();mEnv->expr(forstmt->getCond());Visit(forstmt->getInc())){
           VisitStmt(forstmt->getBody());
       }
   }
   virtual void VisitReturnStmt(ReturnStmt * returnstmt){
       if(mEnv->hasReturn()) return;
       VisitStmt(returnstmt->getRetValue());
       mEnv->ret(returnstmt);
       //VisitStmt(returnstmt);
   }
private:
   Environment * mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
   explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
   	   mVisitor(context, &mEnv) {
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context) {
	   TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
	   mEnv.init(decl);

	   FunctionDecl * entry = mEnv.getEntry();
	   mVisitor.VisitStmt(entry->getBody());
  }
private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public: 
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main (int argc, char ** argv) {
   if (argc > 1) {
       //runToolOnCode 
       clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}
