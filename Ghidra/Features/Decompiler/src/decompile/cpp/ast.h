#pragma once


#include <vector>
#include <string>
#include <functional>

#include "types.h"
#include "funcdata.hh"

class ASTVisitor;
class ValueDecl;

class Type;
class BuiltinType;
class StructType;

// using namespace std;
using std::string;
using namespace ghidra;

/**
 * @brief Yes, this is bad design but I need to leave the unimplementedCode
 * logging function associated with ASTBuilder state and have the toAstType()
 * function be able to call this...but also have these classes be able to call
 * toAstType(). This is my hacky workaround that I'm doing to simply get the job
 * done...I know it's not pretty lol. Once everything works, we can remove this
 * as I don't expect to use the unimplementedCode callback at all
 */
struct ASTCallbacks
{
    std::function<Type*(const Datatype* dt)> toAstTypeCallback;
    // Type* (*toAstTypeCallback)(const Datatype* dt, void* context);
    // void* context;

    // this one is actually called by AST classes
    Type* toAstType(const Datatype* dt)
    {
        return toAstTypeCallback(dt);
        // return toAstTypeCallback(dt, context);
    }
};

void initASTCallbacks(ASTCallbacks* cb);

/**
 * @brief Represents a single node in the AST
 */
class ASTNode
{
public:
    ASTNode();
    virtual ~ASTNode();

    /**
     * @brief Virtual function to just clone this type of node
     * (children will be handled generically by clone())
     */
    virtual ASTNode* clone() = 0;

    inline ASTNode* parent() { return _parent; }
    inline std::vector<ASTNode*>* children() { return &_children; }
    inline std::vector<string>* messages() { return &_messages; }

    /**
     * @brief Adds child to this node's children, if it is not already
     * in the list (the pointer value itself, not the object value).
     *
     * @param child is the child to add
     * @param append If true, add to end of list. Otherwise add to beginning
     */
    void addChild(ASTNode* child, bool append=true, bool check_parens=true);

    /**
     * @brief Removes this node and replaces it with new_node in the AST.
     * This node's children are moved over to become children of new_node,
     * and new_node takes the place of this node with respect to its parent
     *
     * @param replacement
     * @returns a pointer to this node which may now be deleted
     */
    ASTNode* replaceWith(ASTNode* new_node);

    void accept(ASTVisitor*, void* context=nullptr);

    /**
     * @brief Return the precedence level of this node, or -1 if it doesn't
     * apply/doesn't have one.
     *
     * The highest precedence is 1 and the lowest is 17, per the table
     * here:
     * https://www.learncpp.com/cpp-tutorial/operator-precedence-and-associativity/
     */
    virtual int precedence() = 0;

    /**
     * @brief Return true if LR associative, false otherwise. If there is no
     * associativity for this node, returns false.
     */
    virtual bool isLRAssociative() = 0;

    /**
     * @brief True if the next child would be on the left side of this
     * node's operation when we add it via addChild(child, append)
     *
     * Note that since append is not supplied, we assume child will be
     * appended. This logic breaks if we try to prepend a child, which would
     * alter the calculations for an existing child who thought it was the
     * first in the list of children.
     *
     * Example 1: for a + b - c, the subexpr a + b would be left of the - op
     * if added
     *
     * Example 2: for *(char*)xyz, the subexpr (char*)xyz would NOT be left
     * of the * operator if added
     */
    virtual bool wouldNextChildBeLeftOfOp() = 0;

    /**
     * @brief True if this node has a precedence level
    */
    bool hasPrecedence() { return precedence() > 0; }

protected:
    /**
     * @brief Clone all the child nodes recursively
     */
    void clone_children(ASTNode* cloned_node) const
    {
        cloned_node->children()->clear();

        for (auto const& child : _children) {
            cloned_node->children()->push_back(child->clone());
        }
    }

    /**
     * @brief This is the node-specific function to perform the accept
     * on a particular kind of ASTNode. The general accept() handles calling
     * both this function as well as recursing through any child nodes
     */
    virtual void* doAccept(ASTVisitor*, void*) { return nullptr; }

    ASTNode* _parent;       // pointer to existing parent, not our memory
    // dynamically-allocated child pointers we must free when destructed
    std::vector<ASTNode*> _children;
    std::vector<string> _messages;  // diagnostic/error messages for validation
};

class BinaryOperator : public ASTNode
{
public:
    BinaryOperator(std::string opcode);

    virtual BinaryOperator* clone() override
    {
        auto binop = new BinaryOperator(*this);
        clone_children(binop);
        return binop;
    }

    inline std::string opcode() { return _opcode; }

    int precedence();
    bool isLRAssociative();
    bool wouldNextChildBeLeftOfOp();

    /**
     * @brief True if this node has a precedence level
    */
    bool hasPrecedence() { return precedence() > 0; }

protected:

    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
};

class BreakStmt : public ASTNode
{
public:
    BreakStmt();
    virtual BreakStmt* clone() override
    {
        auto bs = new BreakStmt(*this);
        clone_children(bs);
        return bs;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * First child: reference to callee
 * Second child: param 1
 * Third child: param 2
 * ...
 */
class CallExpr : public ASTNode
{
public:
    CallExpr();
    virtual CallExpr* clone() override
    {
        auto expr = new CallExpr(*this);
        clone_children(expr);
        return expr;
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * First child: ConstantExpr case value
 * Second child: code for switch case
 */
class CaseStmt : public ASTNode
{
public:
    CaseStmt();
    virtual CaseStmt* clone() override
    {
        auto stmt = new CaseStmt(*this);
        clone_children(stmt);
        return stmt;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CharacterLiteral : public ASTNode
{
public:
    CharacterLiteral(BuiltinType* type, uintb value);
    virtual ~CharacterLiteral() { delete _type; }

    virtual CharacterLiteral* clone() override;

    inline uintb value() { return _value; }
    inline BuiltinType* type() { return _type; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    BuiltinType* _type;
    uintb _value;
};

/**
 * @brief A list of statements
 */
class CompoundStmt : public ASTNode
{
public:
    CompoundStmt();
    virtual CompoundStmt* clone() override
    {
        auto cs = new CompoundStmt(*this);
        clone_children(cs);
        return cs;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ConstantExpr : public ASTNode
{
public:
    ConstantExpr();
    virtual ConstantExpr* clone() override
    {
        auto expr = new ConstantExpr(*this);
        clone_children(expr);
        return expr;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CStyleCastExpr : public ASTNode
{
public:
    CStyleCastExpr(Type* type);

    virtual CStyleCastExpr* clone() override;

    inline Type* type() { return _type; }

    inline void replace_type(Type* newtype)
    {
        delete _type;
        _type = newtype;
    }

    int precedence() { return 3; }
    // c-style cast actually is R->L
    bool isLRAssociative() { return false; }
    // c-style cast children always to its right
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Type* _type;
};

/**
 * @brief A reference to a declared variable, function, enum, etc.
 */
class DeclRefExpr : public ASTNode
{
public:
    /**
     * @brief Construct a new DeclRefExpr
     *
     * @param referencedDecl is a pointer to the existing ValueDecl* for
     * the referenced variable (already created from its definition). This
     * may be located using the map<>'s in ASTBuilder. Thus DeclRefExpr
     * DOES NOT OWN this memory and must not delete it.
     */
    DeclRefExpr(ValueDecl* referencedDecl);

    virtual DeclRefExpr* clone() override;

    inline ValueDecl* ref() { return _ref; }
    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ValueDecl* _ref;
};

/**
 * @brief Adaptor to allow mixing declarations with statements and expressions.
 */
class DeclStmt : public ASTNode
{
public:
    DeclStmt();

    virtual DeclStmt* clone() override
    {
        auto ds = new DeclStmt(*this);
        clone_children(ds);
        return ds;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * IfStmt children are:
 * - Conditional
 * - Then block
 * - Else block (if present)
 */
class IfStmt : public ASTNode
{
public:
    IfStmt();

    virtual IfStmt* clone() override
    {
        auto stmt = new IfStmt(*this);
        clone_children(stmt);
        return stmt;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class IntegerLiteral : public ASTNode
{
public:
    IntegerLiteral(Type* type, uintb value);

    virtual IntegerLiteral* clone() override;

    inline uintb value() { return _value; }
    inline Type* type() { return _type; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    uintb _value;
    Type* _type;
};

/**
 * @brief Not really part of the AST...
 * I don't want to lose errors or warnings I need to be aware of,
 * and the way the Ghidra decompiler runs in a separate process I may
 * not readily notice silent issues. This allows me to pass through
 * errors into the output JSON for now (later there may be a better way)
 */
class LogMsg : public ASTNode
{
public:
    LogMsg(std::string msg);

    virtual LogMsg* clone() override
    {
        auto msg = new LogMsg(*this);
        clone_children(msg);
        return msg;
    }

    inline std::string message() { return _msg; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _msg;
};

class ParenExpr : public ASTNode
{
public:
    ParenExpr();

    virtual ParenExpr* clone() override
    {
        auto expr = new ParenExpr(*this);
        clone_children(expr);
        return expr;
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }     // actually L->R
    // I think paren children are right of op since they are
    // right of opening paren '('
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Declaration of a structure field (mostly for compatibility with clang
 * AST for validation purposes...)
 */
class FieldDecl : public ASTNode
{
public:
    FieldDecl(string name, Type* type)
        : _name(name), _type(type)
    { }

    virtual FieldDecl* clone() override;

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    string name() { return _name; }
    Type* type() { return _type; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    Type* _type;   // we don't own the dtype here - StructField does
};

/**
 * @brief Declaration of a structure, union, or class
 */
class RecordDecl : public ASTNode
{
public:
    RecordDecl(StructType* stype);

    virtual RecordDecl* clone() override
    {
        auto rd = new RecordDecl(*this);
        clone_children(rd);
        return rd;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    int sid() { return _sid; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _sid;
};

class ReturnStmt : public ASTNode
{
public:
    ReturnStmt();

    virtual ReturnStmt* clone() override
    {
        auto rs = new ReturnStmt(*this);
        clone_children(rs);
        return rs;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class StringLiteral : public ASTNode
{
public:
    StringLiteral(string value);

    virtual StringLiteral* clone() override
    {
        auto lit = new StringLiteral(*this);
        clone_children(lit);
        return lit;
    }

    inline string value() { return _value; }

    // CLS: not sure if we need to have a type for this or not?
    // the other literals do, but all I'm seeing so far in clang AST
    // output is char[10], char[4], char[32], etc. based on the
    // size of the value. If we need it, add it...but not sure yet
    // inline Type* type() { return _type; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);

    string _value;
    // Type* _type;
};

class SwitchStmt : public ASTNode
{
public:
    SwitchStmt();

    virtual SwitchStmt* clone() override
    {
        auto ss = new SwitchStmt(*this);
        clone_children(ss);
        return ss;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Holds all of the structure definitions for some AST code context.
 *
 * Right now this is associated with a TranslationUnit, but I'm adding this
 * layer of abstraction so that if we want to have a single struct type library
 * for an entire program later on (including all its functions and globals) then
 * we have the flexibility to do that - all the code will be written to a
 * StructTypeLibrary instance which we can just move as desired.
 */
class StructTypeLibrary
{
public:
    StructTypeLibrary(int base_id = 0);

    map<int, StructType*>& structures_by_id() { return _structures_by_id; }
    map<string, StructType*>& structures_by_name() { return _structures_by_name; }

    // STRUCT ID OPTIONS
    // 1. Use the Ghidra ID (this won't work if this type is not defined in ghidra already)
    // 2. Autogen a new ID on the fly arbitrarily - increment a counter
    // 3. Compute a deterministic ID - hash the content or something
        // - this is slower
        // - but we can get it from the contents...
        // - we need to take care that we include or exclude the struct name in the
        //   hash if we want to prevent 2 separate structures WITH IDENTICAL CONTENT
        //   to hash to the same id or not

    /**
     * For this use case, we are always coming FROM Ghidra -> TO JSON
     * - if we are adding a new structure definition to Ghidra, our approach
     *   WILL DEFINE IT IN GHIDRA FIRST, then re-decompile, then extract new AST
     *   (not try and define the struct directly in the AST...because then we
     *   don't benefit from the normal data-flow analyses, etc.)
     *
     * If we ever have a use case where we want to add new types directly to
     * the AST then we will just have to add new ids with care
     * - find the existing max id and just keep incrementing
     * - start at a much higher base id (900,000+) if there is risk of Ghidra
     *   subsequently adding a structure or two and now colliding with the
     *   ones we had just added (@ MAX+1)
     */

    /**
     * @brief Looks up the matching StructType for the given Ghidra structure
     * type. If none has been mapped yet, a new StructType is created and added
     * to the library.
     *
     * Since this function performs a new mapping if necessary, a valid StructType
     * pointer will always be returned.
     */
    StructType* getStructTypeForGhidraStruct(TypeStruct* ts);

    /**
     * @brief Returns the StructType* for the given struct id if it exists,
     * otherwise returns nullptr
     */
    StructType* getStructureType(int sid)
    {
        if (_structures_by_id.count(sid)) {
            return _structures_by_id[sid];
        }
        return nullptr;
    }

    /**
     * @brief Returns the StructType* for the given struct name if it exists,
     * otherwise returns nullptr
     */
    StructType* getStructureType(string name)
    {
        if (_structures_by_name.count(name)) {
            return _structures_by_name[name];
        }
        return nullptr;
    }

protected:
    StructType* mapNewStructure(TypeStruct* ts);

    std::map<int, StructType*> _structures_by_id;
    std::map<string, StructType*> _structures_by_name;
    int _next_id;
};

/**
 * @brief Represents a top-level translation unit
 */
class TranslationUnitDecl : public ASTNode
{
public:
    TranslationUnitDecl();

    virtual TranslationUnitDecl* clone() override
    {
        auto tu = new TranslationUnitDecl(*this);
        clone_children(tu);
        return tu;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    /** @brief The structure type library, where all the structure type
     * definitions are stored for this translation unit */
    StructTypeLibrary* type_library() { return &_type_library; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    StructTypeLibrary _type_library;
};

/**
 * CLS: this is where I might end up deviating from Clang a bit...
 * not only in how "Type" is used but whether or not I use BuiltinType,
 * RecordType, etc...
 *
 * OR - if the model is architected such that it is trying to predict the
 * "ASTNode type" for category (e.g. BuiltinType vs. PointerType vs. ArrayType)
 * instead of the first element of the triple, it might be natural to follow
 * the clang pattern here
 *
 * C structs:
 * RecordDecl (name [, size?])
 * |- FieldDecl (name, type, offset)
 * |- FieldDecl (name, type, offset)
 *
 * NOTE:
 * Ok, just because I'm getting confused now on where we are and what I even
 * want for types...I'm going to just MAKE A DECISION to move forward.
 * Then once we get to look at the dataset and evaluate, we can DEFINITELY
 * come back and change/tweak/adjust it to whatever unified solution make sense!
 *
 * BUT FOR NOW...
 *
 * - We have various ASTNode's that have a Datatype* dt() property associated with them
 *      > currently we are just printing the name in JSON
 * >> TODO: go back through and convert all these Datatype* instances in the
 *   AST to Type (ASTNode) instances
 *
 * DIFFICULT TO DECIDE...
 * - Should we have a "type database" that each variable just points to (via type_id?)
 *      - This avoids making the file overly large I guess?
 *      - For structures it definitely does not make sense to splat the definition
 *        all over the place
 *          > SOLUTION: the use of different ASTNodes for different types actually
 *            helps this issue
 *              - BuiltinType: this is just the name - we know how to convert it to a triple
 *              - PointerType: represents a pointer, holds another Type instance for pointed-to type
 *              - ArrayType: probably just like pointer...but with a size
 *              - RecordType: use the "database" concept for structures, and RecordType
 *                just contains their id
 *
 * TODO - PointerType, ArrayType, RecordType
 */

class Type : public ASTNode
{
public:
    Type(const Datatype* dt);
    Type(string name);
    virtual ~Type() {}

    virtual Type* clone() override
    {
        auto t = new Type(*this);
        clone_children(t);
        return t;
    }

    /**
     * @brief The Ghidra Datatype associated with this Type if one exists,
     * otherwise nullptr.
     */
    inline const Datatype* ghidra_dtype() { return _ghidra_dt; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    inline string name() { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    const Datatype* _ghidra_dt;
};

/**
 * @brief Integers, floats
 */
class BuiltinType : public Type
{
public:
    BuiltinType(const Datatype* dt);
    BuiltinType(string name, int size, bool isFloatingPoint, bool sign);

    virtual BuiltinType* clone() override
    {
        auto t = new BuiltinType(*this);
        clone_children(t);
        return t;
    }

    /** @brief Size of type in bytes */
    int size();

    /** @brief True if floating point type, false if integral type */
    bool isFloatingPoint();

    /** @brief True if signed, false if unsigned */
    bool isSigned();

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    bool _is_floating;
    bool _is_signed;
    int _size;
};

/**
 * @brief Array with specified constant size
 */
class ConstantArrayType : public Type
{
public:
    /**
     * @brief The element datatype will be created as a child node,
     * but is provided here as a parameter for convenience and to
     * hopefully reduce mistakes.
     *
     * @param elementType
     * @param numElements
     */
    ConstantArrayType(const Datatype* elementType, int numElements);
    ConstantArrayType(const TypeArray* arrType);

    virtual ConstantArrayType* clone() override
    {
        auto t = new ConstantArrayType(*this);
        clone_children(t);
        return t;
    }

    inline int numElements() { return _num_elements; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _num_elements;
};

/**
 * @brief Pointer type, child node is the pointed-to type
 */
class PointerType : public Type
{
public:
    PointerType(const Datatype* pointedToType);

    virtual PointerType* clone() override
    {
        auto pt = new PointerType(*this);
        clone_children(pt);
        return pt;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Describes a single field within a structure
 */
class StructField
{
public:
    StructField()
        : _name(""), _dtype(nullptr), _offset(0)
    { }

    /**
     * @brief Creates a new StructField object
     *
     * @param name The name of the field
     * @param dtype The type of the field. This will be copied so the caller
     * retains ownership of this memory
     * @param offset
     */
    StructField(string name, Type* dtype, int offset)
        : _name(name), _dtype(dtype), _offset(offset)
    { }

    // StructField(const StructField& other)
    // {
    //     _name = other._name;
    //     if (other._dtype) {
    //         _dtype = other._dtype->clone();
    //     } else {
    //         _dtype = nullptr;
    //     }
    //     _offset = other._offset;
    // }

    // StructField(StructField&& other)
    // {
    //     if (_dtype) {
    //         delete _dtype;
    //     }
    //     _dtype = other._dtype;
    //     _name = other._name;
    //     _offset = other._offset;

    //     other._dtype = nullptr;
    // }

    ~StructField()
    {
        if (_dtype) {
            delete _dtype;
            _dtype = nullptr;
        }
    }

    string name() const { return _name; }
    Type* dtype() const { return _dtype; }
    int offset() const { return _offset; }

protected:
    string _name;
    Type* _dtype;   // we own this and need to clean it up
    int _offset;
    // StructType* _parent;
};

/**
 * @brief Structure type
 *
 * Currently this is an immutable type, and as such the only one constructing
 * these is the StructTypeLibrary
 */
class StructType : public Type
{
    friend class StructTypeLibrary;

protected:
    StructType(const TypeStruct* ts, int sid);

public:
    /**
     * @brief Essentially creates a "lazily loaded" copy of the struct type using
     * only the sid. When the members are invoked, the needed information wil
     * be looked up dynamically from the type_lib.
     *
     * This allows us to create shallow copies of new structures which is useful
     * for recursive structure types (containing pointers referring to themselves).
     * We need to both 1) return a pointer that can be deleted and 2) return a
     * pointer for a struct type that isn't fully initialized yet (in this recursive
     * case). Returning a shallow copy like this allows the pointer to be accessed
     * and deleted as normal by the client, but still satisfy our intialization
     * constrains under the hood.
     */
    StructType(int sid, StructTypeLibrary* type_lib);

    virtual ~StructType()
    {}

    virtual StructType* clone() override
    {
        auto st = new StructType(*this);
        clone_children(st);
        return st;
    }

    /** @brief Structure ID */
    int sid() { return _sid; }

    /** @brief Size of the structure in bytes */
    int size();

    map<int, StructField>& fields();

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _sid;
    int _size;
    map<int, StructField> _fields;  // maps offset -> StructField
    StructTypeLibrary* _type_lib;   // may be nullptr if this is the "real" copy
};

/**
 * @brief Represents the void keyword when it is treated as a data type
 * (void return value, void*, etc.)
 *
 * I was going back and forth on how to represent void (clang AST just has a
 * string "void" inside qualType as far as I can tell right now), but choosing
 * to use a dedicated ASTNode type for now since it can simplify handling it
 * from the perspective of traversing the AST.
 *
 * The other option is to make void a special case of BuiltinType, where it
 * is the only valid BuiltinType to have a size of 0. Something like:
 * { size: 0, sign: ?, isFloating: ? }
 *
 * ...the only downside there is that you then have to make sure your code that
 * processes BuiltinTypes can handle sizes of zero, which might be weird. So
 * I'm representing VoidType as its own node type for now - if we choose to
 * combine it with BuiltinType for our model input data that is fine (and we can
 * always change this later).
 */
class VoidType : public Type
{
public:
    VoidType() : Type("void") {}

    virtual VoidType* clone() override
    {
        auto vt = new VoidType(*this);
        clone_children(vt);
        return vt;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief This declares the typedef
 * (e.g. typedef int foo; )
 */
class TypedefDecl : public ASTNode
{
public:
    TypedefDecl(string name);

    virtual TypedefDecl* clone() override
    {
        auto td = new TypedefDecl(*this);
        clone_children(td);
        return td;
    }

    inline string name() { return _name; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
};

/**
 * @brief This is a "use" of a typedef'd type
 * (e.g. foo myvar; -> foo has a TypedefType type in the AST) *
 */
class TypedefType: public Type
{
public:
    TypedefType(TypedefDecl* decl);

    virtual TypedefType* clone() override
    {
        auto t = new TypedefType(*this);
        clone_children(t);
        return t;
    }

    /** @brief Returns a pointer to the declaration of this typedef */
    inline TypedefDecl* getDecl() { return _decl; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    TypedefDecl* _decl;
};

class UnaryOperator : public ASTNode
{
public:
    UnaryOperator(std::string opcode, Type* type);

    virtual UnaryOperator* clone() override
    {
        auto op = new UnaryOperator(*this);
        op->_type = _type->clone();
        clone_children(op);
        return op;
    }

    inline std::string opcode() { return _opcode; }
    inline Type* type() { return _type; }

    int precedence();
    bool isLRAssociative();
    // for unary operators, children are always on its right!
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
    Type* _type;    // output datatype of operator
};

/**
 * @brief Declaration of a variable (in which case it is an lvalue) a function
 * (in which case it is a function designator) or an enum constant.
 */
class ValueDecl : public ASTNode
{
public:
    /**
     * @param id is a unique ID for this ValueDecl
     */
    ValueDecl(int id);
    virtual ~ValueDecl() {}

    virtual ValueDecl* clone() override
    {
        auto vd = new ValueDecl(*this);
        clone_children(vd);
        return vd;
    }

    inline int id() { return _id; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _id;
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ValueDecl
{
public:
    FunctionDecl(int id, Funcdata* fd);

    virtual FunctionDecl* clone() override
    {
        auto fdecl = new FunctionDecl(*this);
        fdecl->_return_type = _return_type->clone();
        clone_children(fdecl);
        return fdecl;
    }

    inline std::string name() { return _fd->getName(); }
    inline uintb address() { return _fd->getAddress().getOffset(); }
    inline Type* return_type() { return _return_type; }

    inline void replace_return_type(Type* newtype)
    {
        delete _return_type;
        _return_type = newtype;
    }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Funcdata* _fd;
    Type* _return_type;
};

/**
 * @brief Variable declaration or definition
 */
class VarDecl : public ValueDecl
{
public:
    /**
     * @param sym The symbol for this variable
     */
    VarDecl(int id, Symbol* sym);
    VarDecl(int id, string name, Type* type);
    VarDecl(int id, string name, const Datatype* dt);
    virtual ~VarDecl()
    {
        delete _type;
    }

    virtual VarDecl* clone() override
    {
        auto vdecl = new VarDecl(*this);
        vdecl->_type = _type->clone();
        clone_children(vdecl);
        return vdecl;
    }

    inline Symbol* ghidra_sym() { return _sym; }
    inline string name() { return _name; }
    inline Type* type() { return _type; }

    inline void replace_type(Type* newtype)
    {
        delete _type;
        _type = newtype;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Symbol* _sym;
    string _name;
    Type* _type;
};

/**
 * @brief Function parameter declaration
 */
class ParmVarDecl : public VarDecl
{
public:
    ParmVarDecl(int id, ProtoParameter* param);

    virtual ParmVarDecl* clone() override
    {
        auto pv = new ParmVarDecl(*this);
        clone_children(pv);
        return pv;
    }

    /** @brief The backing ProtoParameter for this ParmVarDecl */
    inline ProtoParameter* param() { return _param; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ProtoParameter* _param;
};

