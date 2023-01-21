/*
 * @文件作用: Lua API。实现大量的Lua C API（lua_ *函数）
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 19:21:11
 */


/*
** $Id: lapi.h $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


/* Increments 'L->top', checking for stack overflows */

/// @brief 栈顶指针增加1, 并检查其是否小于当前函数栈顶 
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}


/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this macro
** increases its stack space ('L->ci->top').
*/

/// @brief 调整返回值个数, 当 nres 为 -1 表示不截断返回值个数, 需要调整函数栈顶
#define adjustresults(L,nres) \
    { if ((nres) <= LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }


/* Ensure the stack has at least 'n' elements */
/// @brief 检测栈中元素数量是否足够满足要求, 当前函数调用栈中至少要有 n 个元素
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


/*
** To reduce the overhead of returning from C functions, the presence of
** to-be-closed variables in these functions is coded in the CallInfo's
** field 'nresults', in a way that functions with no to-be-closed variables
** with zero, one, or "all" wanted results have no overhead. Functions
** with other number of wanted results, as well as functions with
** variables to be closed, have an extra check.
*/

/// @brief 是不是to-be-closed 变量
#define hastocloseCfunc(n)	((n) < LUA_MULTRET)

/* Map [-1, inf) (range of 'nresults') into (-inf, -2] */
/// @brief lua中的极大值inf lua中的极小值nan [-1, inf)转变到(-inf, -2]
/// [-1, inf) 转变成[-（-1）- 3，-inf - 3) 因为 -inf - 3 还是 -inf 无穷大加或者减常数=无穷大，如 ：正（或负）无穷大加（或减）3还等于正（或负）无穷大 所以最后是(-inf, -2]
#define codeNresults(n)		(-(n) - 3)
#define decodeNresults(n)	(-(n) - 3)

#endif
