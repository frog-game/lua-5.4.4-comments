/*
 * @文件作用: 标记方法。实现从对象访问元方法。
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 20:58:33
 */

/*
** $Id: ltm.h $
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/

/// @brief 辅助元方法枚举
typedef enum {
  TM_INDEX,// 索引table[key] 访问表中不存在的值
  TM_NEWINDEX,//索引赋值 table[key] = value 对表中不存在的值进行赋值
  TM_GC,//当对象被gc回收时将会调用gc元方法
  TM_MODE,//弱引用表
  TM_LEN,//对应运算符"#" 取对象长度
  TM_EQ,  /* last tag method with fast access *///对应运算符"=="比较两个对象是否相等
  TM_ADD,//对应运算符"+"（加法）操作
  TM_SUB,//对应的运算符 '-'（减法）操作
  TM_MUL,//对应运算符"*"（乘法）操作
  TM_MOD,//对应运算符"%"（取模）操作
  TM_POW,//对应运算符"^"（次方）操作
  TM_DIV,//对应运算符"/"（除法）操作
  TM_IDIV,//对应运算符"//"（向下取整除法）操作
  TM_BAND,//对应运算符"&"（按位与）操作
  TM_BOR,//对应运算符"|"（按位或）操作
  TM_BXOR,//对应运算符"^"（按位异或）操作
  TM_SHL,//对应运算符"<<"（左移）操作
  TM_SHR,//对应运算符">>"（右移）操作
  TM_UNM,//对应的运算符 '-'（取负）操作
  TM_BNOT,//对应运算符"~"（按位非）操作
  TM_LT,//对应的运算符 '<'（小于）操作
  TM_LE,//对应的运算符 '<='（小于等于）操作
  TM_CONCAT,//对应的运算符 '..'(连接）操作
  TM_CALL,//函数调用操作 func(args)
  TM_CLOSE,//被标记为to-be-closed的局部变量，会在超出它的作用域时，调用它的__closed元方法
  TM_N		/* number of elements in the enum *///辅助元方法枚举数量
} TMS;

 
/*
** Mask with 1 in all fast-access methods. A 1 in any of these bits
** in the flag of a (meta)table means the metatable does not have the
** corresponding metamethod field. (Bit 7 of the flag is used for
** 'isrealasize'.)
*/

//设置元表flags的初始值
//~0u 对无符号0取反
//~0u << (6 + 1) 二进制:11111111111111111111111110000000  十进制:4294967168 
// (~(~0u << (TM_EQ + 1)))  二进制:01111111  十进制:127
#define maskflags	(~(~0u << (TM_EQ + 1)))


/*
** Test whether there is no tagmethod.
** (Because tagmethods use raw accesses, the result may be an "empty" nil.)
*/

//是不是等于nil
#define notm(tm)	ttisnil(tm)

///查询某个元表（Table结构体）的元方法
#define gfasttm(g,et,e) ((et) == NULL ? NULL : \
  ((et)->flags & (1u<<(e))) ? NULL : luaT_gettm(et, e, (g)->tmname[e]))

///查询某个元表（Table结构体）的元方法
#define fasttm(l,et,e)	gfasttm(G(l), et, e)

//获取类型的字符串名字
#define ttypename(x)	luaT_typenames_[(x) + 1]

///LUAI_DDEC 等价于extern 函数 其实如果是函数声明不需要额外在声明时加extern，加不加是等价的。
LUAI_DDEC(const char *const luaT_typenames_[LUA_TOTALTYPES];)


LUAI_FUNC const char *luaT_objtypename (lua_State *L, const TValue *o);

LUAI_FUNC const TValue *luaT_gettm (Table *events, TMS event, TString *ename);
LUAI_FUNC const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o,
                                                       TMS event);
LUAI_FUNC void luaT_init (lua_State *L);

LUAI_FUNC void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
LUAI_FUNC void luaT_callTMres (lua_State *L, const TValue *f,
                            const TValue *p1, const TValue *p2, StkId p3);
LUAI_FUNC void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC void luaT_tryconcatTM (lua_State *L);
LUAI_FUNC void luaT_trybinassocTM (lua_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
LUAI_FUNC void luaT_trybiniTM (lua_State *L, const TValue *p1, lua_Integer i2,
                               int inv, StkId res, TMS event);
LUAI_FUNC int luaT_callorderTM (lua_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
LUAI_FUNC int luaT_callorderiTM (lua_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

LUAI_FUNC void luaT_adjustvarargs (lua_State *L, int nfixparams,
                                   struct CallInfo *ci, const Proto *p);
LUAI_FUNC void luaT_getvarargs (lua_State *L, struct CallInfo *ci,
                                              StkId where, int wanted);


#endif
