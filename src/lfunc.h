/*
 * @文件作用: 函数原型及闭包管理
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-27 17:32:19
 */
/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"

//c闭包大小
#define sizeCclosure(n)	(cast_int(offsetof(CClosure, upvalue)) + \
                         cast_int(sizeof(TValue)) * (n))

//lua闭包大小
// 内存布局:| LClosure | UpVal* | .. | UpVal* |
#define sizeLclosure(n)	(cast_int(offsetof(LClosure, upvals)) + \
                         cast_int(sizeof(TValue *)) * (n))


/* test whether thread is in 'twups' list */
//在不在twups链表
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
//上值最大数
#define MAXUPVAL	255

//是不是open的
#define upisopen(up)	((up)->v != &(up)->u.value)

//上值层级深度
// 比如这样的
// uplevel #0 当前层
// uplevel #1 当前层的上一层
#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v))


/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/

//原型函数中闭包缓存最大miss数
#define MAXMISS		10



/* special status to close upvalues preserving the top of the stack */
//调用luaF_close给当时堆栈顶部设置的状态值
#define CLOSEKTOP	(-1)


LUAI_FUNC Proto *luaF_newproto (lua_State *L);
LUAI_FUNC CClosure *luaF_newCclosure (lua_State *L, int nupvals);
LUAI_FUNC LClosure *luaF_newLclosure (lua_State *L, int nupvals);
LUAI_FUNC void luaF_initupvals (lua_State *L, LClosure *cl);
LUAI_FUNC UpVal *luaF_findupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_newtbcupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_closeupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_close (lua_State *L, StkId level, int status, int yy);
LUAI_FUNC void luaF_unlinkupval (UpVal *uv);
LUAI_FUNC void luaF_freeproto (lua_State *L, Proto *f);
LUAI_FUNC const char *luaF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
