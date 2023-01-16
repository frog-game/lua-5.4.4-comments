/*
** $Id: ltm.c $
** Tag methods
** See Copyright Notice in lua.h
*/

#define ltm_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"

/// @brief usedata名字
static const char udatatypename[] = "userdata";

/// @brief lua类型名字
LUAI_DDEF const char *const luaT_typenames_[LUA_TOTALTYPES] = {
  "no value",
  "nil", "boolean", udatatypename, "number",
  "string", "table", "function", udatatypename, "thread",
  "upvalue", "proto" /* these last cases are used for tests only */
};

/// @brief 初始化元方法名字
/// @param L 
void luaT_init (lua_State *L) {
  static const char *const luaT_eventname[] = {  /* ORDER TM */
    "__index", "__newindex",
    "__gc", "__mode", "__len", "__eq",
    "__add", "__sub", "__mul", "__mod", "__pow",
    "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr",
    "__unm", "__bnot", "__lt", "__le",
    "__concat", "__call", "__close"
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = luaS_new(L, luaT_eventname[i]);
    luaC_fix(L, obj2gco(G(L)->tmname[i]));  /* never collect these names *///设置永不回收
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/

/// @brief 根据 元方法名ename 获取 元表events 的元方法，若获取不到则根据元方法标识event设置 元方法events的flags
/// @param events 
/// @param event 
/// @param ename 元方法名
/// @return 
const TValue *luaT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = luaH_getshortstr(events, ename);
  lua_assert(event <= TM_EQ);
  if (notm(tm)) {  /* no tag method? *///若没找到元方法，则标记这个元表的flags，以便下次访问的时候节省查找耗时
    events->flags |= cast_byte(1u<<event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}

/// @brief 根据 元方法标识event 获取 对象o 的元方法
/// @param L 
/// @param o 
/// @param event 
/// @return 
const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttype(o)) {
    case LUA_TTABLE:
      mt = hvalue(o)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(o)];////非table非userdata类型的元表统统在global_State的mt数组内
  }
  return (mt ? luaH_getshortstr(mt, G(L)->tmname[event]) : &G(L)->nilvalue);
}


/*
** Return the name of the type of an object. For tables and userdata
** with metatable, use their '__name' metafield, if present.
*/

/// @brief 返回对象类型的名称。对于带有元表的表和用户数据，请使用其__name元字段（如果存在）
/// @param L 
/// @param o 
/// @return 
const char *luaT_objtypename (lua_State *L, const TValue *o) {
  Table *mt;
  if ((ttistable(o) && (mt = hvalue(o)->metatable) != NULL) ||
      (ttisfulluserdata(o) && (mt = uvalue(o)->metatable) != NULL)) {
    const TValue *name = luaH_getshortstr(mt, luaS_new(L, "__name"));
    if (ttisstring(name))  /* is '__name' a string? */
      return getstr(tsvalue(name));  /* use it as type name */
  }
  return ttypename(ttype(o));  /* else use standard type name */
}

/// @brief 通过fasttm或luaT_gettmbyobj得到元方法后，判断它是否为函数，如果为函数则调用luaT_callTM
/// @param L 
/// @param f function
/// @param p1 1号argument
/// @param p2 2号argument
/// @param p3 3号argument
void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, const TValue *p3) {
  StkId func = L->top;
  setobj2s(L, func, f);  /* push function (assume EXTRA_STACK) */
  setobj2s(L, func + 1, p1);  /* 1st argument */
  setobj2s(L, func + 2, p2);  /* 2nd argument */
  setobj2s(L, func + 3, p3);  /* 3rd argument */
  L->top = func + 4;
  /* metamethod may yield only when called from Lua code */
  if (isLuacode(L->ci))
    luaD_call(L, func, 0);
  else
    luaD_callnoyield(L, func, 0);
}

/// @brief 将元方法,第一,第二操作数先后压栈,再调用并取因返回值
/// @param L 
/// @param f function
/// @param p1 1号argument
/// @param p2 2号argument
/// @param res 栈地址
void luaT_callTMres (lua_State *L, const TValue *f, const TValue *p1,
                     const TValue *p2, StkId res) {
  ptrdiff_t result = savestack(L, res);
  StkId func = L->top;
  setobj2s(L, func, f);  /* push function (assume EXTRA_STACK) */
  setobj2s(L, func + 1, p1);  /* 1st argument */
  setobj2s(L, func + 2, p2);  /* 2nd argument */
  L->top += 3;
  /* metamethod may yield only when called from Lua code */
  if (isLuacode(L->ci))
    luaD_call(L, func, 1);
  else
    luaD_callnoyield(L, func, 1);
  res = restorestack(L, result);
  setobjs2s(L, res, --L->top);  /* move result to its place */
}


static int callbinTM (lua_State *L, const TValue *p1, const TValue *p2,
                      StkId res, TMS event) {
  const TValue *tm = luaT_gettmbyobj(L, p1, event);  /* try first operand *///尝试第一个操作数
  if (notm(tm))//如果是nil
    tm = luaT_gettmbyobj(L, p2, event);  /* try second operand *///尝试第二个操作数
  if (notm(tm)) return 0;//如果是nil
  luaT_callTMres(L, tm, p1, p2, res);
  return 1;
}

/// @brief 尝试调用p1的元表中的元方法，如果调用失败，会抛出运行时异常。
/// @param L 
/// @param p1 
/// @param p2 
/// @param res 
/// @param event 
void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  if (l_unlikely(!callbinTM(L, p1, p2, res, event))) {
    switch (event) {
      case TM_BAND: case TM_BOR: case TM_BXOR:
      case TM_SHL: case TM_SHR: case TM_BNOT: {
        if (ttisnumber(p1) && ttisnumber(p2))
          luaG_tointerror(L, p1, p2);
        else
          luaG_opinterror(L, p1, p2, "perform bitwise operation on");
      }
      /* calls never return, but to avoid warnings: *//* FALLTHROUGH */
      default:
        luaG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}


void luaT_tryconcatTM (lua_State *L) {
  StkId top = L->top;
  if (l_unlikely(!callbinTM(L, s2v(top - 2), s2v(top - 1), top - 2,
                               TM_CONCAT)))
    luaG_concaterror(L, s2v(top - 2), s2v(top - 1));
}

/// @brief 尝试调用元表中的元方法，与luaT_trybinTM不一样的是，这里会根据flip来指定是调用谁的元表
/// @param L 
/// @param p1 
/// @param p2 
/// @param flip 
/// @param res 
/// @param event 
void luaT_trybinassocTM (lua_State *L, const TValue *p1, const TValue *p2,
                                       int flip, StkId res, TMS event) {
  if (flip)
    luaT_trybinTM(L, p2, p1, res, event);
  else
    luaT_trybinTM(L, p1, p2, res, event);
}

/// @brief 尝试调用元表中的元方法
/// @param L 
/// @param p1 
/// @param i2 
/// @param flip 
/// @param res 
/// @param event 
void luaT_trybiniTM (lua_State *L, const TValue *p1, lua_Integer i2,
                                   int flip, StkId res, TMS event) {
  TValue aux;
  setivalue(&aux, i2);
  luaT_trybinassocTM(L, p1, &aux, flip, res, event);
}


/*
** Calls an order tag method.
** For lessequal, LUA_COMPAT_LT_LE keeps compatibility with old
** behavior: if there is no '__le', try '__lt', based on l <= r iff
** !(r < l) (assuming a total order). If the metamethod yields during
** this substitution, the continuation has to know about it (to negate
** the result of r<l); bit CIST_LEQ in the call status keeps that
** information.
*/
int luaT_callorderTM (lua_State *L, const TValue *p1, const TValue *p2,
                      TMS event) {
  if (callbinTM(L, p1, p2, L->top, event))  /* try original event */
    return !l_isfalse(s2v(L->top));
#if defined(LUA_COMPAT_LT_LE)
  else if (event == TM_LE) {
      /* try '!(p2 < p1)' for '(p1 <= p2)' */
      L->ci->callstatus |= CIST_LEQ;  /* mark it is doing 'lt' for 'le' */
      if (callbinTM(L, p2, p1, L->top, TM_LT)) {
        L->ci->callstatus ^= CIST_LEQ;  /* clear mark */
        return l_isfalse(s2v(L->top));
      }
      /* else error will remove this 'ci'; no need to clear mark */
  }
#endif
  luaG_ordererror(L, p1, p2);  /* no metamethod found */
  return 0;  /* to avoid warnings */
}


int luaT_callorderiTM (lua_State *L, const TValue *p1, int v2,
                       int flip, int isfloat, TMS event) {
  TValue aux; const TValue *p2;
  if (isfloat) {
    setfltvalue(&aux, cast_num(v2));
  }
  else
    setivalue(&aux, v2);
  if (flip) {  /* arguments were exchanged? */
    p2 = p1; p1 = &aux;  /* correct them */
  }
  else
    p2 = &aux;
  return luaT_callorderTM(L, p1, p2, event);
}


void luaT_adjustvarargs (lua_State *L, int nfixparams, CallInfo *ci,
                         const Proto *p) {
  int i;
  int actual = cast_int(L->top - ci->func) - 1;  /* number of arguments */
  int nextra = actual - nfixparams;  /* number of extra arguments */
  ci->u.l.nextraargs = nextra;
  luaD_checkstack(L, p->maxstacksize + 1);
  /* copy function to the top of the stack */
  setobjs2s(L, L->top++, ci->func);
  /* move fixed parameters to the top of the stack */
  for (i = 1; i <= nfixparams; i++) {
    setobjs2s(L, L->top++, ci->func + i);
    setnilvalue(s2v(ci->func + i));  /* erase original parameter (for GC) */
  }
  ci->func += actual + 1;
  ci->top += actual + 1;
  lua_assert(L->top <= ci->top && ci->top <= L->stack_last);
}


void luaT_getvarargs (lua_State *L, CallInfo *ci, StkId where, int wanted) {
  int i;
  int nextra = ci->u.l.nextraargs;
  if (wanted < 0) {
    wanted = nextra;  /* get all extra arguments available */
    checkstackGCp(L, nextra, where);  /* ensure stack space */
    L->top = where + nextra;  /* next instruction will need top */
  }
  for (i = 0; i < wanted && i < nextra; i++)
    setobjs2s(L, where + i, ci->func - nextra + i);
  for (; i < wanted; i++)   /* complete required results with nil */
    setnilvalue(s2v(where + i));
}

