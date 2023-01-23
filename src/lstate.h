/*
 * @文件作用: 状态机 管理全局信息,和状态机相关的逻辑
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-23 18:28:47
 */
/*
** $Id: lstate.h $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*
** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).
**
** For the generational collector, some of these lists have marks for
** generations. Each mark points to the first element in the list for
** that particular generation; that generation goes until the next mark.
**
** 'allgc' -> 'survival': new objects;
** 'survival' -> 'old': objects that survived one collection;
** 'old1' -> 'reallyold': objects that became old in last collection;
** 'reallyold' -> NULL: objects old for more than one cycle.
**
** 'finobj' -> 'finobjsur': new objects marked for finalization;
** 'finobjsur' -> 'finobjold1': survived   """";
** 'finobjold1' -> 'finobjrold': just old  """";
** 'finobjrold' -> NULL: really old       """".
**
** All lists can contain elements older than their main ages, due
** to 'luaC_checkfinalizer' and 'udata2finalize', which move
** objects between the normal lists and the "marked for finalization"
** lists. Moreover, barriers can age young objects in young lists as
** OLD0, which then become OLD1. However, a list never contains
** elements younger than their main ages.
**
** The generational collector also uses a pointer 'firstold1', which
** points to the first OLD1 object in the list. It is used to optimize
** 'markold'. (Potentially OLD1 objects can be anywhere between 'allgc'
** and 'reallyold', but often the list has no OLD1 objects or they are
** after 'old1'.) Note the difference between it and 'old1':
** 'firstold1': no OLD1 objects before this point; there can be all
**   ages after it.
** 'old1': no objects younger than OLD1 after this point.
*/

/*
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray (with two exceptions explained below):
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
**
** The exceptions to that "gray rule" are:
** - TOUCHED2 objects in generational mode stay in a gray list (because
** they must be visited again at the end of the cycle), but they are
** marked black because assignments to them must activate barriers (to
** move them back to TOUCHED1).
** - Open upvales are kept gray to avoid barriers, but they stay out
** of gray lists. (They don't even have a 'gclist' field.)
*/



/*
** About 'nCcalls':  This count has two parts: the lower 16 bits counts
** the number of recursive invocations in the C stack; the higher
** 16 bits counts the number of non-yieldable calls in the stack.
** (They are together so that we can change and save both with one
** instruction.)
*/


/* true if this thread does not have non-yieldable calls in the stack */
/// @brief 利用nCcalls 高16位 来判断线程是否可以yield 
#define yieldable(L)		(((L)->nCcalls & 0xffff0000) == 0)

/* real number of C calls */
/// @brief 利用nCcalls 低16位 来作为c函数调用数量
#define getCcalls(L)	((L)->nCcalls & 0xffff)


/* Increment the number of non-yieldable calls */
/// @brief 其实就是利用加nCcalls + 0x10000 这样如果加了,那么yieldable就会认为不能yield
#define incnny(L)	((L)->nCcalls += 0x10000)

/* Decrement the number of non-yieldable calls */
/// @brief 其实就是利用加nCcalls - 0x10000 这样如果减了,那么yieldable 判断如果等于0了那么就认为可以yield
#define decnny(L)	((L)->nCcalls -= 0x10000)

/* Non-yieldable call increment */
/// @brief 自增一下
#define nyci	(0x10000 | 1)



/// @brief goto作用 使用setjmp设置非局部标号
struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/

/// @brief 原子类型的信号,主要是用来保证lua_sethook是线程安全的
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/*
** Extra stack space to handle TM calls and some other extras. This
** space is not included in 'stack_last'. It is used only to avoid stack
** checks, either because the element will be promptly popped or because
** there will be a stack check soon after the push. Function frames
** never use this extra space, so it does not need to be kept clean.
*/

/// @brief 额外的堆栈空间 ,别没事使用这块空间
/// 会留空EXTRA_STACK=5个BUF，用于元表调用或错误处理的栈操作
#define EXTRA_STACK   5

/// @brief 2倍的最小栈空间
#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)

#define stacksize(th)	cast_int((th)->stack_last - (th)->stack)


/* kinds of Garbage Collection */
#define KGC_INC		0	/* incremental gc *///增量gc
#define KGC_GEN		1	/* generational gc *///分代gc

/// @brief Lua用一张hash table来管理所有的字符串资源
typedef struct stringtable {
  TString **hash;//指向字符串的hash表
  int nuse;  /* number of elements *///元素个数
  int size;//hash table 大小 
} stringtable;


/*
** Information about a call.
** About union 'u':
** - field 'l' is used only for Lua functions;
** - field 'c' is used only for C functions.
** About union 'u2':
** - field 'funcidx' is used only by C functions while doing a
** protected call;
** - field 'nyield' is used only while a function is "doing" an
** yield (from the yield until the next resume);
** - field 'nres' is used only while closing tbc variables when
** returning from a function;
** - field 'transferinfo' is used only during call/returnhooks,
** before the function starts or after it ends.
*/
typedef struct CallInfo {
  StkId func;  /* function index in the stack *///该栈位置保存调用关联的函数
  StkId	top;  /* top for this function *///该函数的栈顶引用，[func, top]就是这个函数栈范围
  struct CallInfo *previous, *next;  /* dynamic call link *///双向链表前驱后驱指针
  union {
    struct {  /* only for Lua functions *///针对lua函数
      const Instruction *savedpc;//代码指令执行点, 类似指令寄存器 
      volatile l_signalT trap;//信号软中断开关,做跳转debug使用
      int nextraargs;  /* # of extra arguments in vararg functions *///额外参数
    } l;
    struct {  /* only for C functions *///针对c函数
      lua_KFunction k;  /* continuation in case of yields *///延续函数
      ptrdiff_t old_errfunc;//异常处理
      lua_KContext ctx;  /* context info. in case of yields *///延续函数环境
    } c;
  } u;
  union {
    int funcidx;  /* called-function index *///被调用函数索引
    int nyield;  /* number of values yielded *///yield数量
    int nres;  /* number of values returned *///返回值数量
    struct {  /* info about transferred values (for call/return hooks) */
      unsigned short ftransfer;  /* offset of first value transferred *///与第一个转移值的偏移量 
      unsigned short ntransfer;  /* number of values transferred *///转移的数量
    } transferinfo;
  } u2;
  short nresults;  /* expected number of results from this function *///目标函数返回值个数
  unsigned short callstatus;//调用状态
} CallInfo;


/*
** Bits in CallInfo status
CallInfo->callstatus 字段的位标识
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_C		(1<<1)	/* call is running a C function */
#define CIST_FRESH	(1<<2)	/* call is on a fresh "luaV_execute" frame */
#define CIST_HOOKED	(1<<3)	/* call is running a debug hook */
#define CIST_YPCALL	(1<<4)	/* doing a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_FIN	(1<<7)	/* function "called" a finalizer */
#define CIST_TRAN	(1<<8)	/* 'ci' has transfer information */
#define CIST_CLSRET	(1<<9)  /* function is closing tbc variables */
/* Bits 10-12 are used for CIST_RECST (see below) */
#define CIST_RECST	10
#if defined(LUA_COMPAT_LT_LE)
#define CIST_LEQ	(1<<13)  /* using __lt for __le */
#endif


/*
** Field CIST_RECST stores the "recover status", used to keep the error
** status while closing to-be-closed variables in coroutines, so that
** Lua can correctly resume after an yield from a __close method called
** because of an error.  (Three bits are enough for error status.)
*/
#define getcistrecst(ci)     (((ci)->callstatus >> CIST_RECST) & 7)
#define setcistrecst(ci,st)  \
  check_exp(((st) & 7) == (st),   /* status must fit in three bits */  \
            ((ci)->callstatus = ((ci)->callstatus & ~(7 << CIST_RECST))  \
                                                  | ((st) << CIST_RECST)))


/* active function is a Lua function */
#define isLua(ci)	(!((ci)->callstatus & CIST_C))

/* call is running Lua code (not a hook) */
#define isLuacode(ci)	(!((ci)->callstatus & (CIST_C | CIST_HOOKED)))

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory *////全局使用的内存分配器, 在 lua_auxilib.c 中提供了一个示例: l_alloc
  void *ud;         /* auxiliary data to 'frealloc' *////frealloc 函数的第一个参数, 用来实现定制内存分配器 
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt *///初始为 LG 结构大小
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector *///需要回收的内存数量
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use *///上一轮完整GC 所存活下来的对象总数量
  lu_mem lastatomic;  /* see function 'genstep' in file 'lgc.c' *///上次回收的不良gc计数
  stringtable strt;  /* hash table for strings *///全局的字符串哈希表，即保存那些短字符串，使得整个虚拟机中短字符串只有一份实例
  TValue l_registry;// //保存全局的注册表，注册表就是一个全局的table（即整个虚拟机中只有一个注册表），它只能被C代码访问，通常，它用来保存那些需要在几个模块中共享的数据。比如通过luaL_newmetatable创建的元表就是放在全局的注册表中
  TValue nilvalue;  /* a nil value *///一个空值
  unsigned int seed;  /* randomized seed for hashes *///随机数种子, lstate.c 中的 makeseed 函数生成 
  lu_byte currentwhite;//存放当前GC的白色种类
  lu_byte gcstate;  /* state of garbage collector *///存放GC状态
  lu_byte gckind; /* kind of GC running */            // gc 运行的种类 KGC_INC:增量GC KGC_GEN:分代GC
  lu_byte gcstopem; /* stops emergency collections */ // 为1 停止紧急回收
  lu_byte genminormul;  /* control for minor generational collections *///分代完整GC
  lu_byte genmajormul;  /* control for major generational collections *///分代局部GC
  lu_byte gcstp;  /* control whether GC is running *///GC是否正在运行
  lu_byte gcemergency;  /* true if this is an emergency collection *///为1 进行紧急GC回收
  lu_byte gcpause;  /* size of pause between successive GCs *///触发GC需要等待的内存增长百分比
  lu_byte gcstepmul;  /* GC "speed" *////gc增长速度
  lu_byte gcstepsize;  /* (log2 of) GC granularity *///gc粒度
  GCObject *allgc;  /* list of all collectable objects *///存放待GC对象的链表，所有对象创建之后都会放入该链表中
  GCObject **sweepgc;  /* current position of sweep in list *///待处理的回收数据都存放在rootgc链表中，
                                                                // 由于回收阶段不是一次性全部回收这个链表的所有数据，
                                                                // 所以使用这个变量来保存当前回收的位置，下一次从这个位置开始继续回收操作

  GCObject *finobj;  /* list of collectable objects with finalizers *///存放所有带有析构函数(__gc)的GC obj链表
  GCObject *gray;  /* list of gray objects *///存放灰色节点的链表
  GCObject *grayagain;  /* list of objects to be traversed atomically *///存放需要一次性扫描处理的灰色节点链表，也就是说，这个链表上所有数据的处理需要一步到位，不能被打断
  GCObject *weak;  /* list of tables with weak values *///存放弱值的链表
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) *///键值对（pair），键是弱引用，但键对值的 mark 有如下影响。如果键可达（reachable），则 mark 其值；如果键不可达，则不必 mark 其值
                                                                // 主要用来解决弱表的循环引用问题  
                                                                // 弱引用（weak reference）：可以访问对象，但不会阻止对象被收集。
                                                                //  弱表（weak table）：键或（和）值是弱引用 

  GCObject *allweak;  /* list of all-weak tables *///具有要清除的弱键或弱值 或者弱键弱值同时存在的表
  GCObject *tobefnz;  /* list of userdata to be GC *///所有准备终结的对象
  GCObject *fixedgc;  /* list of objects not to be collected */// 永远不回收的对象链表, 如保留关键字的字符串, 对象必须在创建之后马上
                                                              //  从 allgc 链表移入该链表中, 用的是 lgc.c 中的 luaC_fix 函数 

  /* fields for generational collector */
  GCObject *survival;  /* start of objects that survived one GC cycle *///当前gc存活下来的对象
  GCObject *old1;  /* start of old1 objects *///活过了一次完整的gc
  GCObject *reallyold;  /* objects more than one cycle old ("really old") *///指向被标记为G_OLD的开始节点
  GCObject *firstold1;  /* first OLD1 object in the list (if any) *///列表中的第一个 OLD1 对象
  GCObject *finobjsur;  /* list of survival objects with finalizers *///指向的是带有"__gc"元方法的survival对象
  GCObject *finobjold1;  /* list of old1 objects with finalizers *///指向的是带有"__gc"元方法的old1对象
  GCObject *finobjrold;  /* list of really old objects with finalizers *////指向的是带有"__gc"元方法的被标记为G_OLD对象
  struct lua_State *twups;  /* list of threads with open upvalues *///twups 链表  所有带有 open upvalue 的 thread 都会放到这个链表中，这样提供了一个方便的遍历 thread 的途径，并且排除掉了没有 open upvalue 的 thread
  lua_CFunction panic;  /* to be called in unprotected errors *///代码出现错误且未被保护时，会调用panic函数并终止宿主程。这个函数可以通过lua_atpanic来修改
  struct lua_State *mainthread;//指向主lua_State，或者说是主线程、主执行栈
  TString *memerrmsg;  /* message for memory-allocation errors *///初始为 "not enough memory" 该字符串永远不会被回收
  TString *tmname[TM_N];  /* array with tag-method names *///初始化为元方法字符串, 在 ltm.c luaT_init 中, 且将它们标记为不可回收对象
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types *////保存全局的注册表，注册表就是一个全局的table（即整个虚拟机中只有一个注册表），它只能被C代码访问，通常，它用来保存那些需要在几个模块中共享的数据。比如通过luaL_newmetatable创建的元表就是放在全局的注册表中
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API *///字符串缓存,这个缓存是用于提高字符串访问的命中率的
  lua_WarnFunction warnf;  /* warning function *////警告函数
  void *ud_warn;         /* auxiliary data to 'warnf' */// warnf的辅助数据
} global_State;


/*
** 'per thread' state
*/

/// @brief Lua 主线程栈 数据结构
///作用：管理整个栈和当前函数使用的栈的情况，最主要的功能就是函数调用以及和c的通信
struct lua_State {
  CommonHeader;
  lu_byte status;//当前状态机的状态,LUA_YIELD和LUA_OK为lua_State状态机的状态,这两个状态和协程有这对应关系,详见auxstatus函数
  lu_byte allowhook;//是否允许hook
  unsigned short nci;  /* number of items in 'ci' list *///ci列表中的条目数，存储一共多少个CallInfo
  StkId top;  /* first free slot in the stack *///指向栈的顶部，压入数据，都通过移动栈顶指针来实现
  global_State *l_G;//全局状态机，维护全局字符串表、内存管理函数、gc等信息
  CallInfo *ci;  /* call info for current function *///当前运行函数信息
  StkId stack_last;  /* end of stack (last element + 1) *///执行lua stack最后一个空闲的slot
  StkId stack;  /* stack base *///stack基地址
  UpVal *openupval;  /* list of open upvalues in this stack */// upvalues open状态时候的的链表
  StkId tbclist;  /* list of to-be-closed variables *///此堆栈中所有活动的将要关闭的变量的列表
  GCObject *gclist;//GC列表
  struct lua_State *twups;  /* list of threads with open upvalues *///twups 链表  所有带有 open upvalue 的 thread 都会放到这个链表中，这样提供了一个方便的遍历 thread 的途径，并且排除掉了没有 open upvalue 的 thread
  struct lua_longjmp *errorJmp;  /* current error recover point *///发生错误的长跳转位置，用于记录当函数发生错误时跳转出去的位置
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) *///指向函数调用栈的栈底
  volatile lua_Hook hook;//用户注册的hook回调函数
  ptrdiff_t errfunc;  /* current error handling function (stack index) *///发生错误的回调函数
  l_uint32 nCcalls;  /* number of nested (non-yieldable | C)  calls */// 当前C函数的调用的深度
  int oldpc;  /* last pc traced *///最后一次执行的指令的位置
  int basehookcount;//用户设置的执行指令数（在hookmask=LUA_MASK_COUNT生效）
  int hookcount;//运行时，跑了多少条指令
  volatile l_signalT hookmask;//支持那些hook能力
};

#define G(L)	(L->l_G)

/*
** 'g->nilvalue' being a nil value flags that the state was completely
** build.
*/
#define completestate(g)	ttisnil(&g->nilvalue)


/*
** Union of all collectable objects (only for conversions)
** ISO C99, 6.5.2.3 p.5:
** "if a union contains several structures that share a common initial
** sequence [...], and if the union object currently contains one
** of these structures, it is permitted to inspect the common initial
** part of any of them anywhere that a declaration of the complete type
** of the union is visible."
*/

/// @brief gc对象
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;//字符串
  struct Udata u;//用户数据
  union Closure cl;//闭包
  struct Table h;//表
  struct Proto p;//函数原型:存放函数字节码信息
  struct lua_State th;  /* thread *///线程
  struct UpVal upv;//上值
};


/*
** ISO C99, 6.7.2.1 p.14:
** "A pointer to a union object, suitably converted, points to each of
** its members [...], and vice versa."
*/
#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */

//----------------------------- 转换GCobject到指定类型 begin ----------------------------//
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))//转换成字符串
#define gco2u(o)  check_exp((o)->tt == LUA_VUSERDATA, &((cast_u(o))->u))//转换成userdata
#define gco2lcl(o)  check_exp((o)->tt == LUA_VLCL, &((cast_u(o))->cl.l))//转换成lua闭包
#define gco2ccl(o)  check_exp((o)->tt == LUA_VCCL, &((cast_u(o))->cl.c))//转换成c闭包
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))//转换成函数
#define gco2t(o)  check_exp((o)->tt == LUA_VTABLE, &((cast_u(o))->h))//转换成表
#define gco2p(o)  check_exp((o)->tt == LUA_VPROTO, &((cast_u(o))->p))//转换成函数原型
#define gco2th(o)  check_exp((o)->tt == LUA_VTHREAD, &((cast_u(o))->th)) //转换成线程
#define gco2upv(o)	check_exp((o)->tt == LUA_VUPVAL, &((cast_u(o))->upv)) //转换成上值
//----------------------------- 转换GCobject到指定类型 end ----------------------------//

/*
** macro to convert a Lua object into a GCObject
** (The access to 'tt' tries to ensure that 'v' is actually a Lua object.)
*/

//将lua对象转换成GCObject对象
#define obj2gco(v)	check_exp((v)->tt >= LUA_TSTRING, &(cast_u(v)->gc))


/* actual number of total bytes allocated */
///分配的实际总字节数
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);
LUAI_FUNC void luaE_checkcstack (lua_State *L);
LUAI_FUNC void luaE_incCstack (lua_State *L);
LUAI_FUNC void luaE_warning (lua_State *L, const char *msg, int tocont);
LUAI_FUNC void luaE_warnerror (lua_State *L, const char *where);
LUAI_FUNC int luaE_resetthread (lua_State *L, int status);


#endif

