/*
 * @文件作用: 协程库
 * @功能分类: 内嵌库
 * @注释者: frog-game
 * @LastEditTime: 2023-01-31 21:55:48
 */

/*
** $Id: lcorolib.c $
** Coroutine Library
** See Copyright Notice in lua.h
*/


#define lcorolib_c
#define LUA_LIB

#include "lprefix.h"


#include <stdlib.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/// @brief 获取协程栈
/// @param L 
/// @return 
static lua_State *getco (lua_State *L) {
  lua_State *co = lua_tothread(L, 1);
  luaL_argexpected(L, co, 1, "thread");
  return co;
}


/*
** Resumes a coroutine. Returns the number of results for non-error
** cases or -1 for errors.
*/

/// @brief 启动一个协程
/// @param L 原始线程
/// @param co 要启动的线程
/// @param narg 传入的参数个数
/// @return 
static int auxresume (lua_State *L, lua_State *co, int narg) {
  int status, nres;
  if (l_unlikely(!lua_checkstack(co, narg))) {//检测一下参数是不是合法
    lua_pushliteral(L, "too many arguments to resume");//参数太多，无法继续
    return -1;  /* error flag *///错误标签
  }
  lua_xmove(L, co, narg);//把narg个参数从L转移到co
  status = lua_resume(co, L, narg, &nres);//调用lua_resume，根据返回值处理
  if (l_likely(status == LUA_OK || status == LUA_YIELD)) {
    if (l_unlikely(!lua_checkstack(L, nres + 1))) {
      lua_pop(co, nres);  /* remove results anyway *///
      lua_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    lua_xmove(co, L, nres);  /* move yielded values *///表示协程函数返回或中途有yield操作:将co栈中的返回值全部转移到L栈
    return nres;
  }
  else {//如果返回值不是LUA_OK或LUA_YIELD 出错
    lua_xmove(co, L, 1);  /* move error message *///出错：将co栈顶的错误对象转移到L栈顶
    return -1;  /* error flag */
  }
}

/// @brief coroutinue.resume库函数入口 启动一个协程
/// @param L 
/// @return 
static int luaB_coresume (lua_State *L) {
  lua_State *co = getco(L); //获取协程栈
  int r;
  r = auxresume(L, co, lua_gettop(L) - 1);//启动一个协程
  if (l_unlikely(r < 0)) {//出错了
    lua_pushboolean(L, 0);//错误信息在栈顶，false值插入错误信息下
    lua_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lua_pushboolean(L, 1);//true值插入r个返回值之下
    lua_insert(L, -(r + 1));
    return r + 1;  /* return true + 'resume' returns */
  }
}

/// @brief 和resume类型
/// @param L 
/// @return 
static int luaB_auxwrap (lua_State *L) {
  lua_State *co = lua_tothread(L, lua_upvalueindex(1));//获取协程栈
  int r = auxresume(L, co, lua_gettop(L));//唤醒协程
  if (l_unlikely(r < 0)) {  /* error? */
    int stat = lua_status(co);
    if (stat != LUA_OK && stat != LUA_YIELD) {  /* error in the coroutine? */
      stat = lua_resetthread(co);  /* close its tbc variables */
      lua_assert(stat != LUA_OK);
      lua_xmove(co, L, 1);  /* move error message to the caller */
    }
    if (stat != LUA_ERRMEM &&  /* not a memory error and ... */
        lua_type(L, -1) == LUA_TSTRING) {  /* ... error object is a string? *///若栈顶是字符串类型，则栈顶的为错误信息
      luaL_where(L, 1);  /* add extra info, if available *///获取堆栈信息
      lua_insert(L, -2);//插入错误信息之前
      lua_concat(L, 2);//连接错误信息
    }
    return lua_error(L);  /* propagate error */
  }
  return r;
}

/// @brief 创建协程 入参为协程回调的函数名
// 例如 co = coroutine.create(f) 
// f就是那个回调的函数名
/// @param L 
/// @return 
static int luaB_cocreate (lua_State *L) {
  lua_State *NL;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  NL = lua_newthread(L);//new一个新协程栈
  lua_pushvalue(L, 1);  /* move function to top *///将CallInfo操作栈上的协程回调函数，移动到L->top数据栈顶部
  lua_xmove(L, NL, 1);  /* move function from L to NL *///拷贝回调函数到协程的数据栈上 
  return 1;
}

/// @brief 创建一个协程，但返回的是一个闭包，协程对象作为闭包的upvalue
/// @param L 
/// @return 
static int luaB_cowrap (lua_State *L) {
  luaB_cocreate(L);//创建协程
  lua_pushcclosure(L, luaB_auxwrap, 1);///把线程对象当作luaB_auxwrap的一个upvalue
  return 1;
}

/// @brief 协程挂起函数
//  luaB_yield 调用lua_yield，后面调用lua_yieldk
/// @param L 
/// @return 
static int luaB_yield (lua_State *L) {
  return lua_yield(L, lua_gettop(L));
}


#define COS_RUN		0 //协程运行
#define COS_DEAD	1 //当协程运行完要执行的代码时或者在运行代码时发生了错误(error)
#define COS_YIELD	2 //当一个协程刚被创建时或遇到函数中的yield关键字时
#define COS_NORM	3 //当协程处于活跃状态，但没有被运行（这意味着程序在运行另一个协程，比如从协程A中唤醒协程B，此时A处于正常状态，因为当前运行的是协程B）

/// @brief 状态
// 运行（running）
// 死亡（dead）
// 挂起（suspended）
// 正常（normal）
static const char *const statname[] =
  {"running", "dead", "suspended", "normal"};

/// @brief 获取协程状态
/// @param L 
/// @param co 
/// @return 
static int auxstatus (lua_State *L, lua_State *co) {
  if (L == co) return COS_RUN;
  else {
    switch (lua_status(co)) {
      case LUA_YIELD://coroutinue出让ing 
        return COS_YIELD;
      case LUA_OK: {//若为LUA_OK，看情况
        lua_Debug ar;
        if (lua_getstack(co, 0, &ar))  /* does it have frames? *///若还有栈帧，则说明是normal状态
          return COS_NORM;  /* it is running */
        else if (lua_gettop(co) == 0)//否则，若无参数，则说明协程已死
            return COS_DEAD;
        else
          return COS_YIELD;  /* initial state *///其余情况，则为挂起状态
      }
      default:  /* some error occurred *///其余情况则为死亡状态
        return COS_DEAD;
    }
  }
}

/// @brief 获取当前协程的状态，将状态字符串压栈
/// @param L 
/// @return 
static int luaB_costatus (lua_State *L) {
  lua_State *co = getco(L);
  lua_pushstring(L, statname[auxstatus(L, co)]);
  return 1;
}

/// @brief 挂起协程
/// @param L 
/// @return 
static int luaB_yieldable (lua_State *L) {
  lua_State *co = lua_isnone(L, 1) ? L : getco(L);
  lua_pushboolean(L, lua_isyieldable(co));
  return 1;
}

/// @brief 获取当前正在执行的协程，压栈
/// @param L 
/// @return 
static int luaB_corunning (lua_State *L) {
  int ismain = lua_pushthread(L);
  lua_pushboolean(L, ismain);
  return 2;
}

/// @brief 只能在挂起或死亡状态下调用，挂起状态下会使用协程进入死亡状态，并且关闭所有的close变量
/// @param L 
/// @return 
static int luaB_close (lua_State *L) {
  lua_State *co = getco(L);
  int status = auxstatus(L, co);
  switch (status) {
    case COS_DEAD: case COS_YIELD: {
      status = lua_resetthread(co);
      if (status == LUA_OK) {
        lua_pushboolean(L, 1);
        return 1;
      }
      else {
        lua_pushboolean(L, 0);
        lua_xmove(co, L, 1);  /* move error message */
        return 2;
      }
    }
    default:  /* normal or running coroutine */
      return luaL_error(L, "cannot close a %s coroutine", statname[status]);
  }
}

// create和wrap都是用来创建协同程序的，
// 不同的是create返回的是一个线程号(一个协同程序就是一个线程)，
// 并且创建的协同程序处于suspend状态，必须用resume唤醒协同程序执行，执行完之后协同程序也就处于dead状态。
// 而wrap则是返回一个函数，一但调用这个函数就进入coroutine状态。
static const luaL_Reg co_funcs[] = {
  {"create", luaB_cocreate},
  {"resume", luaB_coresume},
  {"running", luaB_corunning},
  {"status", luaB_costatus},
  {"wrap", luaB_cowrap},
  {"yield", luaB_yield},
  {"isyieldable", luaB_yieldable},
  {"close", luaB_close},
  {NULL, NULL}
};



LUAMOD_API int luaopen_coroutine (lua_State *L) {
  luaL_newlib(L, co_funcs);
  return 1;//返回1，表示将这个表返回给lua
}

