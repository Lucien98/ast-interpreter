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
       VisitStmt(bop);
	   mEnv->binop(bop);
   }
   virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
       VisitStmt(expr);
	   mEnv->declref(expr);
   }
   virtual void VisitCallExpr(CallExpr * call) {
       VisitStmt(call);
	   mEnv->call(call, 0);
       std::string sys_fun("GET, PRINT, MALLOC, FREE");
       if(FunctionDecl *funcdecl = call->getDirectCallee()){
           if(sys_fun.find(funcdecl->getName())==std::string::npos){
               try {
                   VisitStmt(funcdecl->getBody());
               }catch(ReturnException &e){};
               mEnv->call(call, 1);
           }
       }
   }
   virtual void VisitDeclStmt(DeclStmt * declstmt) {
	   mEnv->decl(declstmt);
   }
   virtual void VisitIfStmt(IfStmt * ifstmt){
       Visit(ifstmt->getCond());
       mEnv->getcond(ifstmt->getCond())?Visit(ifstmt->getThen()):\
           ifstmt->getElse()? Visit(ifstmt->getElse()):(void)0;
   }
   virtual void VisitWhileStmt(WhileStmt * whilestmt){
      while(Visit(whilestmt->getCond()),mEnv->getcond(whilestmt->getCond())){
           VisitStmt(whilestmt->getBody());
       }
   }
   virtual void VisitForStmt(ForStmt * forstmt){
     for(forstmt->getInit();Visit(forstmt->getCond()),mEnv->getcond(forstmt->getCond());Visit(forstmt->getInc())){
           VisitStmt(forstmt->getBody());
       }
   }
   virtual void VisitReturnStmt(ReturnStmt * returnstmt){
       VisitStmt(returnstmt);
       mEnv->ret(returnstmt);
   }
   virtual void VisitIntegerLiteral(IntegerLiteral * integer){
       mEnv->intliteral(integer);
   }
   virtual void VisitUnaryOperator(UnaryOperator * unaryexpr){
       VisitStmt(unaryexpr);
       mEnv->unaryexpr(unaryexpr);
   }
   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr *array){
       VisitStmt(array);
       mEnv->arraysub(array);
   }
   virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr * sizeofexpr){
       VisitStmt(sizeofexpr);
       mEnv->sizeofexpr(sizeofexpr);
   }
   virtual void VisitCastExpr(CastExpr * cast){
       VisitStmt(cast);
       mEnv->cast(cast);
   }
   virtual void VisitParenExpr(ParenExpr * paren){
       VisitStmt(paren);
       mEnv->paren(paren);
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
       try{
	        mVisitor.VisitStmt(entry->getBody());
       }catch(ReturnException &e){}
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
