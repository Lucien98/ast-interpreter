//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include <exception>
using namespace std;
using namespace clang;

class ReturnException : public std::exception{};
class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, int64_t> mVars;
   std::map<Stmt*, int64_t> mExprs;
   /// The current stmt
   Stmt * mPC;
   int64_t retValue;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, int64_t val) {
      mVars[decl] = val;
   }    
   int64_t getDeclVal(Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   int64_t bindStmt(Stmt * stmt, int64_t val) {
	   return mExprs[stmt] = val;
   }
   int64_t getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }
   void setRetValue(int64_t ret){
       retValue=ret;
   }
   int64_t getRetValue(){
       return retValue;
   }
};

class Environment {
   std::vector<StackFrame> mStack;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
   }


   /// Initialize the Environment
   void init(TranslationUnitDecl * unit) {
	   mStack.push_back(StackFrame());
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }else{
             if(VarDecl * vardecl = dyn_cast<VarDecl>(*i)){
			   if(vardecl->getType().getTypePtr()->isIntegerType() ||vardecl->getType().getTypePtr()->isCharType()){
                   if(vardecl->hasInit()) {
                       if (auto literal = dyn_cast<IntegerLiteral>(vardecl->getInit())){
                           intliteral(literal); 
                           mStack.back().bindDecl(vardecl, literal->getValue().getSExtValue());
                       }
                   }
                    else mStack.back().bindDecl(vardecl, 0);
                }
               }
           }
	   }
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   int64_t binop(BinaryOperator *bop) {
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();
       auto opCode = bop->getOpcode();
       int64_t leftVal = mStack.back().getStmtVal(left),
               rightVal = mStack.back().getStmtVal(right);
       int64_t val;
       switch(opCode){
           case BO_Assign:
               if(DeclRefExpr *declExpr = dyn_cast<DeclRefExpr>(left)){
                   mStack.back().bindStmt(left, rightVal);
                   Decl *decl = declExpr->getFoundDecl();
                   mStack.back().bindDecl(decl, rightVal);
               }else if(auto array = dyn_cast<ArraySubscriptExpr>(left)){
                   int64_t base = mStack.back().getStmtVal(array->getLHS()->IgnoreImpCasts()),
                           offset = mStack.back().getStmtVal(array->getRHS()->IgnoreImpCasts());
                   *(int64_t *)(base+offset*8) = rightVal;
                   mStack.back().bindStmt(left, rightVal);
              }else if(auto unaryexpr = dyn_cast<UnaryOperator>(left)){
                   * (int64_t *)(mStack.back().getStmtVal(unaryexpr->getSubExpr()))=rightVal;
                   mStack.back().bindStmt(left, rightVal);
              }
               break;
            case BO_Add:
               if(left->getType().getTypePtr()->isPointerType()){
                   leftVal += sizeof(int64_t)*rightVal;
                   mStack.back().bindStmt(bop, val = leftVal);
               }else
               mStack.back().bindStmt(bop, val = leftVal + rightVal);
               break;
            case BO_Sub:
               mStack.back().bindStmt(bop, val = leftVal - rightVal);
               break;
            case BO_Mul:
               mStack.back().bindStmt(bop, val = leftVal * rightVal);
               break;
            case BO_Div:
               rightVal==0?printf("Error:number cannot be divided by zero\n"),exit(0):(void)0;
               leftVal%rightVal==0?0:printf("Warning: number is not divided with no remainder\n");
               mStack.back().bindStmt(bop, val = int64_t(leftVal/rightVal));
               break;
            case BO_LT:
               mStack.back().bindStmt(bop, val = (leftVal<rightVal));
               break;
            case BO_GT:
               mStack.back().bindStmt(bop, val = (leftVal>rightVal));
               break;
            case BO_EQ:
               mStack.back().bindStmt(bop, val = (leftVal==rightVal));
               break;
            default:
               break;
       }
        return val;
   }

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
           if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
               const Type * type = vardecl->getType().getTypePtr();
               if(type->isIntegerType() || type->isCharType() || type->isPointerType()){
                    if(vardecl->hasInit()) 
                        mStack.back().bindDecl(vardecl, 
                                (dyn_cast<IntegerLiteral>(vardecl->getInit())->getValue().getSExtValue()));
                    else mStack.back().bindDecl(vardecl, 0);
               }else{
                   if(auto array = dyn_cast<ConstantArrayType>(vardecl->getType().getTypePtr())){
                       int length = array->getSize().getSExtValue();
                       if(array->getElementType().getTypePtr()->isIntegerType()){
                           int * int_array = new int[length];
                           for(int i=0; i<length; int_array[i++]=0);
                           mStack.back().bindDecl(vardecl, (int64_t)int_array);
                       }else if(array->getElementType().getTypePtr()->isCharType()){
                           char * char_array = new char[length];
                           for(int i=0; i<length; char_array[i++]=0);
                           mStack.back().bindDecl(vardecl, (int64_t)char_array);
                       }else{
                           int64_t ** ptr_array = new int64_t*[length];
                           for(int i=0; i<length; ptr_array[i++]=0);
                           mStack.back().bindDecl(vardecl, (int64_t)ptr_array);
                       }
                   }
               }
		   }
	   }
   }

   int64_t declref(DeclRefExpr * declref) {
	   mStack.back().setPC(declref);
       auto type = declref->getType();
	   mStack.back().bindStmt(declref, 0);//for MALLOC function, it is a declrefexpr but it was not bound
       if (type->isIntegerType() || type->isPointerType() || type->isArrayType()) {
		   Decl* decl = declref->getFoundDecl();
		   int64_t val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
           return val;
	   }
       return 0;
   }
  void ret(ReturnStmt * returnstmt){
       mStack.back().setRetValue(
               mStack.back().getStmtVal(returnstmt->getRetValue()));
       throw ReturnException();
   }

   /// !TODO Support Function Call
   void call(CallExpr * callexpr, int hasInitStack) {
       mStack.back().setPC(callexpr);
       int64_t val = 0;
       FunctionDecl * callee = callexpr->getDirectCallee();
      if (hasInitStack==0){
           if (callee == mInput) {
              llvm::errs() << "Please Input an Integer Value : ";
              scanf("%d", &val);

              mStack.back().bindStmt(callexpr, val);
           } else if (callee == mOutput) {
               Expr * decl = callexpr->getArg(0);
               val = mStack.back().getStmtVal(decl);
               llvm::errs() << val << " ";
           } else if (callee == mMalloc){
               /// You could add your code here for Function call Return
               int size = mStack.back().getStmtVal(callexpr->getArg(0));
               int64_t *p = (int64_t *)malloc(size);
               mStack.back().bindStmt(callexpr, (int64_t)p);
           }else if (callee == mFree){
               std::free( (int64_t *) (mStack.back().getStmtVal(callexpr->getArg(0))) );
           }else{
               vector<int64_t> args;
               for (auto i=callexpr->arg_begin(), e=callexpr->arg_end(); i!=e; args.push_back(mStack.back().getStmtVal(*(i++))));
               mStack.push_back(StackFrame());
               int j = 0;
               for (auto i=callee->param_begin(), e=callee->param_end(); i!=e; i++,j++)
                   mStack.back().bindDecl(*i, args[j]);
               }
       }else{
           int64_t retvalue = mStack.back().getRetValue();
           mStack.pop_back();
           mStack.back().bindStmt(callexpr, retvalue);
       }
   }

   void intliteral(IntegerLiteral * integer){
       mStack.back().bindStmt(integer, integer->getValue().getSExtValue());
   }

   void cast(CastExpr * cast){
       if (cast->getType()->isIntegerType() || cast->getType()->isPointerType()){
           int64_t val = mStack.back().getStmtVal(cast->getSubExpr());
           mStack.back().bindStmt(cast, val);
       }
   }

   void sizeofexpr(UnaryExprOrTypeTraitExpr * sizeofexpr){
       if(sizeofexpr->getArgumentType()->isPointerType())
           mStack.back().bindStmt(sizeofexpr, sizeof(int64_t *));
       else
           mStack.back().bindStmt(sizeofexpr, sizeof(int64_t));
   }

   void unaryexpr(UnaryOperator * unaryexpr){
       auto opcode = unaryexpr->getOpcode();
       int64_t val = mStack.back().getStmtVal(unaryexpr->getSubExpr());
       if(opcode == UO_Minus){
           mStack.back().bindStmt(unaryexpr, -1*val);
       }else if(opcode = UO_Deref){
           mStack.back().bindStmt(unaryexpr, *(int64_t *)val);
       }
   }

   void paren(ParenExpr * paren){
       int64_t val = mStack.back().getStmtVal(paren->getSubExpr());
       mStack.back().bindStmt(paren, val);
   }
   
   void arraysub(ArraySubscriptExpr * array){
       int64_t base = mStack.back().getStmtVal(array->getLHS()->IgnoreImpCasts()),
               offset = mStack.back().getStmtVal(array->getRHS()->IgnoreImpCasts());
       mStack.back().bindStmt(array, *(int64_t *)(base+offset*8));
   }

   int64_t getcond(Expr * expr){
       return mStack.back().getStmtVal(expr);
   }
};


