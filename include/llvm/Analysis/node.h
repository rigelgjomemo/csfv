#ifndef NODE_H
#define NODE_H

#include <iostream>
#include <sstream>
#include <vector>
#include <set>
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

class ACSLStatement;
class ACSLExpression;

typedef std::vector<ACSLStatement*> StatementList;
typedef std::vector<ACSLExpression*> ExpressionList;



class ACSLNode {//abstract
public:
    enum NodeKind{
        ACSLStatementsKind,
        ACSLAssertStatementKind,
        ACSLEnsuresStatementKind,
        ACSLRequiresStatementKind,
        ACSLBinaryExpressionKind,
        ACSLIdentifierKind,
        ACSLIntegerKind,
        ACSLDoubleKind
    };
    
    ACSLNode * parent;
    // Later we need to do some runtime type-checking. In order to do this, type
    // information must be provided by the compiler. In order to do that, it is
    // necessary to generate a virtual table for the class of the object to cast.
    // If that class does not have any virtual method, the default trick is to
    // define an empty virtual destructor, just to force compiler emitting the
    // virtual table.
    virtual ~ACSLNode() {}
    virtual std::string getTreePrintString() = 0;
    virtual std::set<std::string> getTreeVariables() = 0;
    virtual void changeTreeVariableName(const std::string& newName,const std::string& oldName) = 0;

private:
    const NodeKind Kind;
public:
    NodeKind getKind() const { return Kind; }
    ACSLNode(NodeKind K): Kind(K){}
 
};


class ACSLExpression : public ACSLNode {//abstract
public:
    bool hasParenthesis = false;
    
    void setHasParenthesis(bool hasPar){
        hasParenthesis = hasPar;
    }
    
    virtual std::string getTreePrintString() = 0;
    virtual std::set<std::string> getTreeVariables() = 0;
    virtual void changeTreeVariableName(const std::string& newName,const std::string& oldName) = 0;
    ACSLExpression(NodeKind K): ACSLNode(K){} 
    

};

class ACSLStatement : public ACSLNode {//abstract
public:
    int type;
    ACSLExpression& exp;
    ACSLStatement(ACSLExpression& exp, int type, NodeKind K) : ACSLNode(K), type(type), exp(exp) {}
    
    virtual std::string getTreePrintString() = 0;
    
    std::set<std::string> getTreeVariables(){
        return exp.getTreeVariables();
    }
    
    void changeTreeVariableName(const std::string& newName,const std::string& oldName){
        exp.changeTreeVariableName(newName, oldName);
    };
};

class ACSLAssertStatement : public ACSLStatement {
public:
    virtual ~ACSLAssertStatement(){}
    
    ACSLAssertStatement(ACSLExpression& exp, int type) : ACSLStatement(exp, type, ACSLAssertStatementKind) {}

    std::string getTreePrintString(){
        std::string s ="assert " + exp.getTreePrintString();
        return s;
    }
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLAssertStatementKind;
    }

};

class ACSLRequiresStatement : public ACSLStatement {
public:
    ACSLRequiresStatement(ACSLExpression& exp, int type) : ACSLStatement(exp, type, ACSLRequiresStatementKind) {}

    std::string getTreePrintString(){
        std::string s ="requires " + exp.getTreePrintString();
        return s;
    }
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLRequiresStatementKind;
    }
};

class ACSLEnsuresStatement : public ACSLStatement {
public:
    ACSLEnsuresStatement(ACSLExpression& exp, int type) : ACSLStatement(exp, type, ACSLEnsuresStatementKind) {}

    std::string getTreePrintString(){
        std::string s ="ensures " + exp.getTreePrintString();
        return s;
    }
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLEnsuresStatementKind;
    }
};

class ACSLBinaryExpression : public ACSLExpression {
public:
   int op;
   ACSLExpression& lhs;
   ACSLExpression& rhs;
   ACSLBinaryExpression(ACSLExpression& lhs, int op, ACSLExpression& rhs) : ACSLExpression(ACSLBinaryExpressionKind),
        op(op), lhs(lhs), rhs(rhs) { }
    
    virtual ~ACSLBinaryExpression(){}

    std::string getTreePrintString(){
        
        std::ostringstream o;
        
        if(hasParenthesis){
            o <<'(';
        }
        
        o << lhs.getTreePrintString() << " ";

        switch(op)
        {
            case 1:
                o << "==";
            break;
            case 2:
                o << "!=";
            break;
            case 3:
                o << "<";
            break;
            case 4:
                o << ">";
            break;
            case 5:
                o << "<=";
            break;
            case 6:
                o << ">=";
            break;
            case 7:
                o << "&&";
            break;
            case 8:
                o << "||";
            break;
            default:
                o << "??";
            break;
        }
        
        

        o << " " << rhs.getTreePrintString();
        
        if(hasParenthesis){
            o <<')';
        }
        
        return o.str();
    }
    
    std::set<std::string> getTreeVariables(){
        std::set<std::string> variablesList = lhs.getTreeVariables();
        
        std::set<std::string> rhsVarList = rhs.getTreeVariables();
        
        for (std::set<std::string>::iterator j = rhsVarList.begin(); j != rhsVarList.end(); ++j)
        {
            variablesList.insert(*j);
        }
        
        return variablesList;
    }
    
    void changeTreeVariableName(const std::string& newName,const std::string& oldName){
        lhs.changeTreeVariableName(newName, oldName);
        rhs.changeTreeVariableName(newName, oldName);
    };
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLBinaryExpressionKind;
    }
};

class ACSLInteger : public ACSLExpression {
public:
    long long value;
    ACSLInteger(long long value) : ACSLExpression(ACSLIntegerKind), value(value) { }
    virtual ~ACSLInteger(){}

    std::string getTreePrintString(){
        std::ostringstream o;
        o << value;
        return o.str();
    }
    
    std::set<std::string> getTreeVariables(){
        std::set<std::string> variablesList;
        return variablesList;
    }
    
    void changeTreeVariableName(const std::string& newName,const std::string& oldName){};
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLIntegerKind;
    }
    
};

class ACSLDouble : public ACSLExpression {
public:
    double value;
    ACSLDouble(double value) : ACSLExpression(ACSLDoubleKind), value(value) { }

    std::string getTreePrintString(){
        std::ostringstream o;
        o << value;
        return o.str();
    }
    
    std::set<std::string> getTreeVariables(){
        std::set<std::string> variablesList;
        return variablesList;
    }
    
    void changeTreeVariableName(const std::string& newName,const std::string& oldName){};
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLDoubleKind;
    }
    
};

class ACSLIdentifier : public ACSLExpression {
public:
    std::string name;
    llvm::Value * llvmvalue;
    ACSLIdentifier(const std::string& name) : ACSLExpression(ACSLIdentifierKind), name(name), llvmvalue(NULL) {}
    
    virtual ~ACSLIdentifier(){}

    std::string getTreePrintString(){
        if(hasParenthesis){
            std::ostringstream o;
            o <<'('<<name<<')';
            return o.str();
        }
        else {
            return name;
        }
    }
    
    std::set<std::string> getTreeVariables(){
        std::set<std::string> variablesList;
        
        variablesList.insert(name);
        
        return variablesList;
    }
    
    void changeTreeVariableName(const std::string& newName,const std::string& oldName){
        if(name == oldName){
            name = newName;
        }
    };
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLIdentifierKind;
    }

};

class ACSLStatements : public ACSLNode{
public:
    StatementList statements;
    
    ACSLStatements() : ACSLNode(ACSLStatementsKind) { }
    
    std::string getTreePrintString(){
        std::ostringstream o;
        for (std::vector<ACSLStatement*>::iterator i = statements.begin(); i != statements.end(); ++i)
        {  
            o << (*i)->getTreePrintString() << "\n";
        }
        return o.str();
    }
    
    std::set<std::string> getTreeVariables(){
        std::set<std::string> variablesList;
        for (std::vector<ACSLStatement*>::iterator i = statements.begin(); i != statements.end(); ++i)
        {
            std::set<std::string> statementVarList = (*i)->getTreeVariables();
            
            for (std::set<std::string>::iterator j = statementVarList.begin(); j != statementVarList.end(); ++j)
            {
                variablesList.insert(*j);
            }
        }
        return variablesList;
    }

    void changeTreeVariableName(const std::string& newName,const std::string& oldName){
        for (std::vector<ACSLStatement*>::iterator i = statements.begin(); i != statements.end(); ++i)
        {
            (*i)->changeTreeVariableName(newName, oldName);
        }
    };
    
    static bool classof(const ACSLNode *S) {
        return S->getKind() == ACSLStatementsKind;
    }
};


/*std::pair<int,int> getRange(AcslStatements root) {
	
}*/

#endif // NODE_H
