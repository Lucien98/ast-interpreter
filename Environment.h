//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, int> mVars;
   std::map<Stmt*, int> mExprs;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, int val) {
      mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   int bindStmt(Stmt * stmt, int val) {
	   return mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

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
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }
	   }
	   mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   int binop(BinaryOperator *bop) {
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();

	   /*if (bop->isAssignmentOp()) {
		   int val = mStack.back().getStmtVal(right);
		   mStack.back().bindStmt(left, val);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
			   Decl * decl = declexpr->getFoundDecl();
			   mStack.back().bindDecl(decl, val);
		   }
	   }*/
       auto opCode = bop->getOpcode();
       int leftVal = expr(left), rightVal = expr(right);
       int val;
       switch(opCode){
           case BO_Assign:
               val = mStack.back().getStmtVal(right);
               mStack.back().bindStmt(left, val);
               if(DeclRefExpr *declExpr = dyn_cast<DeclRefExpr>(left)){
                   Decl *decl = declExpr->getFoundDecl();
                   mStack.back().bindDecl(decl, val);
               }
               break;
            case BO_Add:
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
               mStack.back().bindStmt(bop, val = int(leftVal/rightVal));
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
		  /*I think the if statement is not necessary*/
           // if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
             VarDecl * vardecl = dyn_cast<VarDecl>(decl);
			   if(vardecl->getType().getTypePtr()->isIntegerType() ||vardecl->getType().getTypePtr()->isCharType()){
                    if(vardecl->hasInit()) mStack.back().bindDecl(vardecl, expr(vardecl->getInit()));
               }
		  // }
	   }
   }

    int expr(Expr * expression){
        expression = expression->IgnoreImpCasts();//what if we did not ignore the implicit cast?
        if(auto literal = dyn_cast<IntegerLiteral>(expression)){             //int expr
            return literal->getValue().getSExtValue();
        }else if (auto literal = dyn_cast<CharacterLiteral>(expression)){    //char expr
            return literal->getValue();
        }else if(auto unaryExpr = dyn_cast<UnaryOperator>(expression)){      //unary expr
            auto opcode = unaryExpr->getOpcode();
            Expr * subExpr = unaryExpr->getSubExpr();
            if(opcode == UO_Minus){
                return mStack.back().bindStmt(unaryExpr, -1*expr(subExpr));
            }else if(opcode == UO_Plus){
                return mStack.back().bindStmt(unaryExpr, expr(subExpr));
            }
        }else if(auto declRef = dyn_cast<DeclRefExpr>(expression)){          //declref expr
            return declref(declRef);
        }else if(auto bop = dyn_cast<BinaryOperator>(expression)){           //binary operator expr
            return binop(bop);
        }else if(auto paren = dyn_cast<ParenExpr>(expression)){              //parenthesis expr
            return expr(paren->getSubExpr());

        }
        return 0;
    }

   int declref(DeclRefExpr * declref) {
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();

		   int val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
           return val;
	   }
       return 0;
   }

   void cast(CastExpr * castexpr) {
	   mStack.back().setPC(castexpr);
	   if (castexpr->getType()->isIntegerType()) {
		   Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmt(castexpr, val );
	   }
   }

   /// !TODO Support Function Call
   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);

		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val;
	   } else {
		   /// You could add your code here for Function call Return
	   }
   }
};


