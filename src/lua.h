/*
** $Id: lua.h $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>


#include "luaconf.h"


#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"4"
#define LUA_VERSION_RELEASE	"4"

#define LUA_VERSION_NUM			504
#define LUA_VERSION_RELEASE_NUM		(LUA_VERSION_NUM * 100 + 4)

#define LUA_VERSION	"Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE "  Copyright (C) 1994-2022 Lua.org, PUC-Rio"
#define LUA_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


/* mark for precompiled code ('<esc>Lua') */
#define LUA_SIGNATURE	"\x1bLua"

/* option for multiple returns in 'lua_pcall' and 'lua_call' */
#define LUA_MULTRET	(-1)


/*
** Pseudo-indices
** (-LUAI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
#define LUA_REGISTRYINDEX	(-LUAI_MAXSTACK - 1000)
#define lua_upvalueindex(i)	(LUA_REGISTRYINDEX - (i))


/* thread status */
#define LUA_OK		0
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRERR	5


typedef struct lua_State lua_State;


/*
** basic types
*/
#define LUA_TNONE		(-1) //无类型

#define LUA_TNIL		0 //空类型
#define LUA_TBOOLEAN		1 //bool
#define LUA_TLIGHTUSERDATA	2 // light userdata(需要关注内存释放)
#define LUA_TNUMBER		3 //双精度浮点数
#define LUA_TSTRING		4 //字符串
#define LUA_TTABLE		5 //表
#define LUA_TFUNCTION		6 //函数
#define LUA_TUSERDATA		7 //full userdata（不需要关注内存释放）
#define LUA_TTHREAD		8 //线程 这里的线程不是传统意义上的os线程,和协程有很大挂钩

#define LUA_NUMTYPES		9 //基本类型 总数



/* minimum Lua stack available to a C function */
/// @brief 最小栈空间
#define LUA_MINSTACK	20


/* predefined values in the registry */
/// @brief 注册表中的预定义值
#define LUA_RIDX_MAINTHREAD	1 //指向main thread
#define LUA_RIDX_GLOBALS	2 //指向global table
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS //保存了Globals(也就是_G)


/* type of numbers in Lua */
/// @brief 数字类型
typedef LUA_NUMBER lua_Number;


/* type for integer functions */
/// @brief 整型类型
typedef LUA_INTEGER lua_Integer;

/* unsigned integer type */
/// @brief 无符号整数类型
typedef LUA_UNSIGNED lua_Unsigned;

/* type for continuation-function contexts */
/// @brief 延续函数上下文的类型
typedef LUA_KCONTEXT lua_KContext;


/*
** Type for C functions registered with Lua
*/
/// @brief 在 Lua 注册的 C 函数的类型
typedef int (*lua_CFunction) (lua_State *L);

/*
** Type for continuation functions
*/

/// @brief 延续函数类型
typedef int (*lua_KFunction) (lua_State *L, int status, lua_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
*/

/// @brief 用来读取/写入 加载/转储 lua块
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

/// @brief 写入数据接口
typedef int (*lua_Writer) (lua_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/

/// @brief 内存分配函数
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** Type for warning functions
*/

/// @brief 警告函数
typedef void (*lua_WarnFunction) (void *ud, const char *msg, int tocont);


/*
** generic extra include file
*/
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/*
** RCS ident string
*/
/// @brief 一些版本,作者信息
extern const char lua_ident[];


/*
** state manipulation
*/

/// @brief 构建一台虚拟机global_State以及一条执行线程lua_State 
/// @param  
/// @return 
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);

/// @brief 关闭虚拟机和所有的执行线程
/// @param L 
/// @return 
LUA_API void       (lua_close) (lua_State *L);

/// @brief 创建一条执行线程
/// @param  
/// @return 
LUA_API lua_State *(lua_newthread) (lua_State *L);

/// @brief 重置一条线程
/// @param L 
/// @return 
LUA_API int        (lua_resetthread) (lua_State *L);

/// @brief 设置panic回调函数 
/// @param  
/// @return 
LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);

/// @brief 这里返回了指针,预防静态链接问题以及中途值被修改的问题 
/// @param  
/// @return 
LUA_API lua_Number (lua_version) (lua_State *L);


/*
** basic stack manipulation
*/

/// @brief 求解有效的堆栈栈顶指针
/// @param L 
/// @param idx 
/// @return 
LUA_API int   (lua_absindex) (lua_State *L, int idx);

/// @brief  栈内已压入的参数个数(元素的函数指针不算)
/// @param L 
/// @return 
LUA_API int   (lua_gettop) (lua_State *L);

/// @brief 调整栈内参数个数, idx>=0：保留N个元素,idx<0:表示保留至倒数第N个参数
// 这里idx的参数意义实际上和absindex()中的一样，也和Lua接口文档一致
/// @param L 
/// @param idx 
/// @return 
LUA_API void  (lua_settop) (lua_State *L, int idx);

/// @brief 拷贝一份idx指定的数据到栈顶并++top 
/// @param L 
/// @param idx 
/// @return 
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);

/// @brief 旋转statck[idx,n]
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API void  (lua_rotate) (lua_State *L, int idx, int n);

/// @brief 拷贝数据fromidx->toidx
/// @param L 
/// @param fromidx 
/// @param toidx 
/// @return 
LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);

/// @brief 调整堆栈大小确保空闲的slot>=n 
/// @param L 
/// @param n 
/// @return 
LUA_API int   (lua_checkstack) (lua_State *L, int n);

LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** access functions (stack -> C)
*/

//----------------------------------判断栈上元素是否为指定(或兼容)类型 begin -------------------------------//
LUA_API int             (lua_isnumber) (lua_State *L, int idx);
LUA_API int             (lua_isstring) (lua_State *L, int idx);
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);
LUA_API int             (lua_isinteger) (lua_State *L, int idx);
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);
//----------------------------------判断栈上元素是否为指定(或兼容)类型 end -------------------------------//

/// @brief 获取类型
/// @param L 
/// @param idx 
/// @return 
LUA_API int             (lua_type) (lua_State *L, int idx);

/// @brief 获取类型名字
/// @param L 
/// @param tp 
/// @return 
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

//----------------------------------栈上指定元素转到对应的类型  begin -------------------------------//
LUA_API lua_Number      (lua_tonumberx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Integer     (lua_tointegerx) (lua_State *L, int idx, int *isnum);
LUA_API int             (lua_toboolean) (lua_State *L, int idx);
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API lua_Unsigned    (lua_rawlen) (lua_State *L, int idx);
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);
//----------------------------------栈上指定元素转到对应的类型 end -------------------------------//


/*
** Comparison and arithmetic functions
*/

#define LUA_OPADD	0	/* ORDER TM, ORDER OP */
#define LUA_OPSUB	1
#define LUA_OPMUL	2
#define LUA_OPMOD	3
#define LUA_OPPOW	4
#define LUA_OPDIV	5
#define LUA_OPIDIV	6
#define LUA_OPBAND	7
#define LUA_OPBOR	8
#define LUA_OPBXOR	9
#define LUA_OPSHL	10
#define LUA_OPSHR	11
#define LUA_OPUNM	12
#define LUA_OPBNOT	13

LUA_API void  (lua_arith) (lua_State *L, int op);


#define LUA_OPEQ	0 // ==
#define LUA_OPLT	1 // < 
#define LUA_OPLE	2 // <=

LUA_API int   (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int   (lua_compare) (lua_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
LUA_API void        (lua_pushnil) (lua_State *L);
LUA_API void        (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void        (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void  (lua_pushboolean) (lua_State *L, int b);
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int   (lua_pushthread) (lua_State *L);


/*
** get functions (Lua -> stack)
*/
LUA_API int (lua_getglobal) (lua_State *L, const char *name);
LUA_API int (lua_gettable) (lua_State *L, int idx);
LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawget) (lua_State *L, int idx);
LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p);

LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdatauv) (lua_State *L, size_t sz, int nuvalue);
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API int  (lua_getiuservalue) (lua_State *L, int idx, int n);


/*
** set functions (stack -> Lua)
*/
LUA_API void  (lua_setglobal) (lua_State *L, const char *name);
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_seti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API int   (lua_setiuservalue) (lua_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Lua code)
*/
LUA_API void  (lua_callk) (lua_State *L, int nargs, int nresults,
                           lua_KContext ctx, lua_KFunction k);
#define lua_call(L,n,r)		lua_callk(L, (n), (r), 0, NULL)

LUA_API int   (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc,
                            lua_KContext ctx, lua_KFunction k);
#define lua_pcall(L,n,r,f)	lua_pcallk(L, (n), (r), (f), 0, NULL)

LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
LUA_API int  (lua_yieldk)     (lua_State *L, int nresults, lua_KContext ctx,
                               lua_KFunction k);
LUA_API int  (lua_resume)     (lua_State *L, lua_State *from, int narg,
                               int *nres);
LUA_API int  (lua_status)     (lua_State *L);
LUA_API int (lua_isyieldable) (lua_State *L);

#define lua_yield(L,n)		lua_yieldk(L, (n), 0, NULL)


/*
** Warning-related functions
*/
LUA_API void (lua_setwarnf) (lua_State *L, lua_WarnFunction f, void *ud);
LUA_API void (lua_warning)  (lua_State *L, const char *msg, int tocont);


/*
** garbage-collection function and options
*/

#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7
#define LUA_GCISRUNNING		9
#define LUA_GCGEN		10
#define LUA_GCINC		11

LUA_API int (lua_gc) (lua_State *L, int what, ...);


/*
** miscellaneous functions
*/

LUA_API int   (lua_error) (lua_State *L);

LUA_API int   (lua_next) (lua_State *L, int idx);

LUA_API void  (lua_concat) (lua_State *L, int n);
LUA_API void  (lua_len)    (lua_State *L, int idx);

LUA_API size_t   (lua_stringtonumber) (lua_State *L, const char *s);

LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void      (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);

LUA_API void (lua_toclose) (lua_State *L, int idx);
LUA_API void (lua_closeslot) (lua_State *L, int idx);


/*
** {==============================================================
** some useful macros
** ===============================================================
*/

#define lua_getextraspace(L)	((void *)((char *)(L) - LUA_EXTRASPACE))

#define lua_tonumber(L,i)	lua_tonumberx(L,(i),NULL)
#define lua_tointeger(L,i)	lua_tointegerx(L,(i),NULL)

#define lua_pop(L,n)		lua_settop(L, -(n)-1)

#define lua_newtable(L)		lua_createtable(L, 0, 0)

#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

#define lua_pushcfunction(L,f)	lua_pushcclosure(L, (f), 0)

#define lua_isfunction(L,n)	(lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE) /// @brief 判断index处元素是否为一个table , 如果是返回1,否则返回0
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L,n)		(lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)
#define lua_isnone(L,n)		(lua_type(L, (n)) == LUA_TNONE)
#define lua_isnoneornil(L, n)	(lua_type(L, (n)) <= 0)

///@brief  通常在push字符串字面值时使用lua_pushliteral,在push字符串指针是使用lua_pushstring "" s这种写法是为了强制只能传递字符串
#define lua_pushliteral(L, s)	lua_pushstring(L, "" s)

/// @brief 将lua的全局表放在栈顶
#define lua_pushglobaltable(L)  \
	((void)lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS))

/// @brief 函数返回一个指向字符串的内部拷贝的指针。你不能修改它（使你想起那里有一个const）。
/// 只要这个指针对应的值还在栈内，Lua会保证这个指针一直有效。
/// 当一个C函数返回后，Lua会清理他的栈，所以，有一个原则：永远不要将指向Lua字符串的指针保存到访问他们的外部函数中
#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)

/// @brief 移动栈顶元素到指定索引的位置，并将这个索引位置上面的元素全部上移至栈顶被移动留下的空隔
#define lua_insert(L,idx)	lua_rotate(L, (idx), 1)

/// @brief 从给定有效索引处移除一个元素， 把这个索引之上的所有元素移下来填补上这个空隙
#define lua_remove(L,idx)	(lua_rotate(L, (idx), -1), lua_pop(L, 1))

/// @brief 把栈顶元素移动到给定位置（并且把这个栈顶元素弹出）， 不移动任何元素（因此在那个位置处的值被覆盖掉）
#define lua_replace(L,idx)	(lua_copy(L, -1, (idx)), lua_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros
** ===============================================================
*/
#if defined(LUA_COMPAT_APIINTCASTS)

#define lua_pushunsigned(L,n)	lua_pushinteger(L, (lua_Integer)(n))
#define lua_tounsignedx(L,i,is)	((lua_Unsigned)lua_tointegerx(L,i,is))
#define lua_tounsigned(L,i)	lua_tounsignedx(L,(i),NULL)

#endif

#define lua_newuserdata(L,s)	lua_newuserdatauv(L,s,1)
#define lua_getuservalue(L,idx)	lua_getiuservalue(L,idx,1)
#define lua_setuservalue(L,idx)	lua_setiuservalue(L,idx,1)

#define LUA_NUMTAGS		LUA_NUMTYPES //基础类型枚举分界线

/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct lua_Debug lua_Debug;  /* activation record */

/* Functions to be called by the debugger in specific events */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar);
LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_getupvalue) (lua_State *L, int funcindex, int n);
LUA_API const char *(lua_setupvalue) (lua_State *L, int funcindex, int n);

LUA_API void *(lua_upvalueid) (lua_State *L, int fidx, int n);
LUA_API void  (lua_upvaluejoin) (lua_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook (lua_gethook) (lua_State *L);
LUA_API int (lua_gethookmask) (lua_State *L);
LUA_API int (lua_gethookcount) (lua_State *L);

LUA_API int (lua_setcstacklimit) (lua_State *L, unsigned int limit);

struct lua_Debug {
  int event;//事件类型标识如下几种 [LUA_HOOKCALL,LUA_HOOKRET,LUA_HOOKLINE,LUA_HOOKCOUNT,LUA_HOOKTAILCALL]
  const char *name;	/* (n) */// 给定函数的一个合理的名字。
                            //  因为Lua中的函数是"first-class values"，所以它们没有固定的名字。
                            //  一些函数可能是全局复合变量的值，另一些可能仅仅只是被保存在一个"table"的某个域中。
                            //  Lua会检查函数是怎样被调用的，以此来找到一个适合的名字。
                            //  如果它找不到名字，该域就被设置为"NULL"。

  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */// 作用域的含义，用于解释"name"域。
                                                                      // 其值可以是"global"，"local"，"method"，"field"，"upvalue"，或是""，
                                                                      // 这取决于函数怎样被调用。（Lua用空串表示其它选项都不符合）

  const char *what;	/* (S) 'Lua', 'C', 'main', 'tail' */// 如果函数是一个Lua函数，则为一个字符串"Lua"；
                                                       //  如果是一个C函数，则为"C"；
                                                       //  如果是一个"chunk"的主体部分，则为"main"。
                                                       //  "what"可以指定如下参数，以指定返回值"table"中包含上面所有域中的哪些域：
                                                       //  'n': 包含"name"和"namewhat"域；
                                                       //  'S': 包含"source"，"short_src"，"linedefined"，"lastlinedefined"以及"what"域；
                                                       //  'l': 包含"currentline"域；
                                                       //  't': 包含"istailcall"域；
                                                       //  'u': 包含"nup"，"nparams"以及"isvararg"域；
                                                       //  'f': 包含"func"域；
                                                       //  'L': 包含"activelines"域；]]

  const char *source;	/* (S) */// 创建这个函数的"chunk"的名字。 
                              //  如果"source"以'@'打头，表示这个函数定义在一个文件中，而'@'之后的部分就是文件名。
                              //  若"source"以'='打头，表示之后的部分由用户行为来决定如何表示源码。
                              //  其它的情况下，这个函数定义在一个字符串中，而"source"正是那个字符串。

  size_t srclen;	/* (S) *///source的长度
  int currentline;	/* (l) *///给定函数正在执行的那一行。当提供不了行号信息的时候，"currentline"被设为-1。
  int linedefined;	/* (S) *///函数定义开始处的行号。
  int lastlinedefined;	/* (S) *///函数定义结束处的行号
  unsigned char nups;	/* (u) number of upvalues *///上值的个数
  unsigned char nparams;/* (u) number of parameters *///参数数量
  char isvararg;        /* (u) *///如果函数是一个可变参数函数则为"true"（对于C函数永远为"true"）。
  char istailcall;	/* (t) *///如果函数以尾调用形式调用，这个值为"true"。在这种情况下，当前栈级别的调用者不在栈中。 所谓尾调用形式，就是一个函数返回另一个函数的返回值,类似于goto语句 
                            //形如
                            // function fun()
                            //   -- body
                            //   return fun1()
	                          // end

  unsigned short ftransfer;   /* (r) index of first value transferred *///与第一个转移值的偏移量 主要用call/return方式
  unsigned short ntransfer;   /* (r) number of transferred values *///转移的数量 主要用call/return方式
  char short_src[LUA_IDSIZE]; /* (S) */// 一个“可打印版本”的"source"，用于出错信息。
  /* private part */
  struct CallInfo *i_ci;  /* active function *///记录一个函数调用涉及到的栈引用，lua在调用函数的时候会把每个callinfo用双向链表串起来
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2022 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
