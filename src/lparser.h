/*
** $Id: lparser.h $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/*
** Expression and variable descriptor.
** Code generation for variables and expressions can be delayed to allow
** optimizations; An 'expdesc' structure describes a potentially-delayed
** variable/expression. It has a description of its "main" value plus a
** list of conditional jumps that can also produce its value (generated
** by short-circuit operators 'and'/'or').
*/

/* kinds of variables/expressions */
typedef enum {
  VVOID,  /* when 'expdesc' describes the last expression of a list,
             this kind means an empty list (so, no expression) *///表达式是空的，也就是void
  VNIL,  /* constant nil *///表达式是nil类型
  VTRUE,  /* constant true *///表达式是true
  VFALSE,  /* constant false *///表达式是false
  VK,  /* constant in 'k'; info = index of constant in 'k' *///表达式是常量类型，expdesc的info字段表示，这个常量是常量表k中的哪个值
  VKFLT,  /* floating constant; nval = numerical float value *////表达式是float常量，expdesc的nval字段表示
  VKINT,  /* integer constant; ival = numerical integer value *///表达式是integer常量，expdesc的ival字段表示
  VKSTR,  /* string constant; strval = TString address;
             (string is fixed by the lexer) *///表达式是string常量，expdesc的strval字段表示
  VNONRELOC,  /* expression has its value in a fixed register;
                 info = result register *///表达式已经在某个寄存器上，expdesc的info字段，表示该寄存器的位置
  VLOCAL,  /* local variable; var.ridx = register index;
              var.vidx = relative index in 'actvar.arr'  *///表达式是local变量，expdesc的info字段表示，var.ridx:寄存器保存变量 var.vidx:actvar.arr中保存变量,使用的相对索引
  VUPVAL,  /* upvalue variable; info = index of upvalue in 'upvalues' *///表达式是Upvalue，expdesc的info字段表示，表示Upvalue数组的索引
  VCONST,  /* compile-time <const> variable;
              info = absolute index in 'actvar.arr'  *///编译时常量 expdesc的info字段表示 actvar.arr中保存变量,使用的绝对索引
  VINDEXED,  /* indexed variable;
                ind.t = table register;
                ind.idx = key's R index *///表示变量索引类型，expdesc的ind字段表示
  VINDEXUP,  /* indexed upvalue;
                ind.t = table upvalue;
                ind.idx = key's K index *///表示上值索引类型，expdesc的ind字段表示
  VINDEXI, /* indexed variable with constant integer;//具有常量整数的索引变量类型 expdesc的ind字段表示
                ind.t = table register;
                ind.idx = key's value */
  VINDEXSTR, /* indexed variable with literal string;
                ind.t = table register;
                ind.idx = key's K index *////表示字符串字母常量索引类型 expdesc的ind字段表示
  VJMP,  /* expression is a test/comparison; 
            info = pc of corresponding jump instruction *///表达式是跳转类型 expdesc的info字段表示 info指向跳转指令
  VRELOC,  /* expression can put result in any register;
              info = instruction pc *///表达式是返回类型 expdesc的info字段表示 info指向指令
  VCALL,  /* expression is a function call; info = instruction pc *///表达式是函数调用，expdesc中的info字段，info指向指令
  VVARARG  /* vararg expression; info = instruction pc *///表达式是 参数 expdesc中的info字段，info指向指令
} expkind;


#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXSTR)
#define vkisindexed(k)	(VINDEXED <= (k) && (k) <= VINDEXSTR)


typedef struct expdesc {
  expkind k;//表达式的类型
  union {
    lua_Integer ival;    /* for VKINT *///表达式是integer常量
    lua_Number nval;  /* for VKFLT *///表达式是float常量
    TString *strval;  /* for VKSTR *///表达式是string常量
    int info;  /* for generic use *///通用
    struct {  /* for indexed variables *///变量索引
      short idx;  /* index (R or "long" K) */
      lu_byte t;  /* table (register or upvalue) */
    } ind;
    struct {  /* for local variables *///局部变量索引
      lu_byte ridx;  /* register holding the variable *///寄存器保存变量
      unsigned short vidx;  /* compiler index (in 'actvar.arr')  *///编译器索引
    } var;
  } u;
  int t;  /* patch list of 'exit when true' */// 真时退出的patch列表
  int f;  /* patch list of 'exit when false' */// 假时退出的patch列表
} expdesc;


/* kinds of variables */
#define VDKREG		0   /* regular *///正则
#define RDKCONST	1   /* constant *///运行时常量会在运行编译完成后的代码时，先在内存的常量区域开辟空间存放常量的值，以确保常量值不可变，保证程序的正确性

/// to-be-closed 特性 to-be-closed特性是让lua的对象具有终结器一样的功能,当变量超出访问范围后即会触发其__close元方法 类似这样
// local t = {}
// setmetatable(t, {__close=function() 
// print('close') 
// end})

// local function test()
//     local tt<close> = t;
// end
// test()
#define RDKTOCLOSE	2 

 /* compile-time constant *///编译时常量 ？
// 而现实中存在一些情况，例如1+2的值恒定不变，或者某个函数在某种情况下的返回值可以由程序员人为确定，并确保程序可以正确运行，而不需要通过存放到内存上的常量区域来保证，这就为代码的优化提供了可能。
// 编译期常量是由编译器在编译时通过计算来确定常量的值，然后在代码中直接进行替换，类似于 #define MAX 5 在编译时将代码中所有的MAX替换成5一样，就不需要在后面的运行时再在内存上开辟空间存放常量，以达到优化的效果。
#define RDKCTC		3  


/* description of an active local variable */
typedef union Vardesc {
  struct {
    TValuefields;  /* constant value (if it is a compile-time constant) */
    lu_byte kind;
    lu_byte ridx;  /* register holding the variable */
    short pidx;  /* index of the variable in the Proto's 'locvars' array */
    TString *name;  /* variable name */
  } vd;
  TValue k;  /* constant value (if any) */
} Vardesc;



/* description of pending goto statements and label statements */
typedef struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  lu_byte nactvar;  /* number of active variables in that position */
  lu_byte close;  /* goto that escapes upvalues */
} Labeldesc;


/* list of labels or gotos */
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
typedef struct Dyndata {
  struct {  /* list of all active local variables */
    Vardesc *arr;
    int n;
    int size;
  } actvar;
  Labellist gt;  /* list of pending gotos */
  Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
typedef struct FuncState {
  Proto *f;  /* current function header */// 主要存放虚拟机指令，常量表
  struct FuncState *prev;  /* enclosing function *///父函数体指针
  struct LexState *ls;  /* lexical state *///词法状态
  struct BlockCnt *bl;  /* chain of current blocks *///当前块链
  int pc;  /* next position to code (equivalent to 'ncode') *///下一个指令，应当存放在Proto结构中的code列表的位置
  int lasttarget;   /* 'label' of last 'jump label' *///最后一个跳转标签
  int previousline;  /* last line that was saved in 'lineinfo' *///保存在行信息中的最后一行
  int nk;  /* number of elements in 'k' *///当前常量的数量
  int np;  /* number of elements in 'p' *///被编译的代码，Proto的数量
  int nabslineinfo;  /* number of elements in 'abslineinfo' *///绝对行信息
  int firstlocal;  /* index of first local var (in Dyndata array) */// 第一个local变量的位置
  int firstlabel;  /* index of first label (in 'dyd->label->arr') *///第一个label位置
  short ndebugvars;  /* number of elements in 'f->locvars' *///局部变量调试信息数量
  lu_byte nactvar;  /* number of active local variables *///当前活跃变量的数量
  lu_byte nups;  /* number of upvalues */// 当前upvalue的数量
  lu_byte freereg;  /* first free register *///下一个可被使用的，空闲寄存器的位置
  lu_byte iwthabs;  /* instructions issued since last absolute line info *///绝对行计数
  lu_byte needclose;  /* function needs to close upvalues when returning *///是否需要关闭上值
} FuncState;


LUAI_FUNC int luaY_nvarstack (FuncState *fs);
LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


#endif
