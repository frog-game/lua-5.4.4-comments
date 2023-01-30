/*
 * @文件作用: 函数原型及闭包管理
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-30 10:16:49
 */
/*
** $Id: lfunc.c $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#define lfunc_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"


/// @brief new一个c闭包
/// @param L 
/// @param nupvals 上值数量
/// @return 
CClosure *luaF_newCclosure (lua_State *L, int nupvals) {
  GCObject *o = luaC_newobj(L, LUA_VCCL, sizeCclosure(nupvals));
  CClosure *c = gco2ccl(o);
  c->nupvalues = cast_byte(nupvals);
  return c;
}

/// @brief new一个lua闭包
/// @param L 
/// @param nupvals 上值数量
/// @return 
LClosure *luaF_newLclosure (lua_State *L, int nupvals) {
  GCObject *o = luaC_newobj(L, LUA_VLCL, sizeLclosure(nupvals));
  LClosure *c = gco2lcl(o);
  c->p = NULL;
  c->nupvalues = cast_byte(nupvals);
  while (nupvals--) c->upvals[nupvals] = NULL;
  return c;
}


/*
** fill a closure with new closed upvalues
*/

/// @brief 设置闭包里的upvalue，默认为close，且值为nil
/// @param L 
/// @param cl 
void luaF_initupvals (lua_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {
    GCObject *o = luaC_newobj(L, LUA_VUPVAL, sizeof(UpVal));
    UpVal *uv = gco2upv(o);
    uv->v = &uv->u.value;  /* make it closed *///设置close状态
    setnilvalue(uv->v);
    cl->upvals[i] = uv;
    luaC_objbarrier(L, cl, uv);
  }
}


/*
** Create a new upvalue at the given level, and link it to the list of
** open upvalues of 'L' after entry 'prev'.
**/

/// @brief 新建一个UpVal的函数
/// @param L 
/// @param tbc 是不是tbc变量
/// @param level upvalue深度
/// @param prev openupval链表的链头
/// @return 
static UpVal *newupval (lua_State *L, int tbc, StkId level, UpVal **prev) {
  GCObject *o = luaC_newobj(L, LUA_VUPVAL, sizeof(UpVal));
  UpVal *uv = gco2upv(o);
  UpVal *next = *prev;
  uv->v = s2v(level);  /* current value lives in the stack */
  uv->tbc = tbc;
  uv->u.open.next = next;  /* link it to list of open upvalues */
  uv->u.open.previous = prev;
  if (next)
    next->u.open.previous = &uv->u.open.next;
  *prev = uv;
  if (!isintwups(L)) {  /* thread not in list of threads with upvalues? *///如果没有在twups链表
    L->twups = G(L)->twups;  /* link it to the list *///链接到twups链表
    G(L)->twups = L;
  }
  return uv;
}


/*
** Find and reuse, or create if it does not exist, an upvalue
** at the given level.
*/

/// @brief 寻找open地址为level的upvalue，若找不到则新建一个
/// @param L 
/// @param level 
/// @return 
UpVal *luaF_findupval (lua_State *L, StkId level) {
  UpVal **pp = &L->openupval;
  UpVal *p;
  lua_assert(isintwups(L) || L->openupval == NULL);
  while ((p = *pp) != NULL && uplevel(p) >= level) {  /* search for it *///查找open的uv, open的uv由L->openupval串起来一个链表
    lua_assert(!isdead(G(L), p));
    if (uplevel(p) == level)  /* corresponding upvalue? */
      return p;  /* return it */
    pp = &p->u.open.next;
  }
  /* not found: create a new upvalue after 'pp' *///如果未找到，创建一个新的加入链表
  return newupval(L, 0, level, pp);
}


/*
** Call closing method for object 'obj' with error message 'err'. The
** boolean 'yy' controls whether the call is yieldable.
** (This function assumes EXTRA_STACK.)
*/

/// @brief 调用__closed元方法
/// @param L 
/// @param obj 
/// @param err 
/// @param yy 
static void callclosemethod (lua_State *L, TValue *obj, TValue *err, int yy) {
  StkId top = L->top;
  const TValue *tm = luaT_gettmbyobj(L, obj, TM_CLOSE);
  setobj2s(L, top, tm);  /* will call metamethod... */
  setobj2s(L, top + 1, obj);  /* with 'self' as the 1st argument */
  setobj2s(L, top + 2, err);  /* and error msg. as 2nd argument */
  L->top = top + 3;  /* add function and arguments */
  if (yy)
    luaD_call(L, top, 0);
  else
    luaD_callnoyield(L, top, 0);
}


/*
** Check whether object at given level has a close metamethod and raise
** an error if not.
*/

/// @brief 检测值是否有__closed元方法,没有就报错
/// @param L 
/// @param level 
static void checkclosemth (lua_State *L, StkId level) {
  const TValue *tm = luaT_gettmbyobj(L, s2v(level), TM_CLOSE);
  if (ttisnil(tm)) {  /* no metamethod? */
    int idx = cast_int(level - L->ci->func);  /* variable index */
    const char *vname = luaG_findlocal(L, L->ci, idx, NULL);
    if (vname == NULL) vname = "?";
    luaG_runerror(L, "variable '%s' got a non-closable value", vname);
  }
}


/*
** Prepare and call a closing method.
** If status is CLOSEKTOP, the call to the closing method will be pushed
** at the top of the stack. Otherwise, values can be pushed right after
** the 'level' of the upvalue being closed, as everything after that
** won't be used again.
*/

/// @brief 调用__closed元方法前的一些预处理
/// @param L 
/// @param level 
/// @param status 
/// @param yy 
static void prepcallclosemth (lua_State *L, StkId level, int status, int yy) {
  TValue *uv = s2v(level);  /* value being closed */
  TValue *errobj;
  if (status == CLOSEKTOP)
    errobj = &G(L)->nilvalue;  /* error object is nil */
  else {  /* 'luaD_seterrorobj' will set top to level + 2 */
    errobj = s2v(level + 1);  /* error object goes after 'uv' */
    luaD_seterrorobj(L, status, level + 1);  /* set error object */
  }
  callclosemethod(L, uv, errobj, yy);
}


/*
** Maximum value for deltas in 'tbclist', dependent on the type
** of delta. (This macro assumes that an 'L' is in scope where it
** is used.)
*/

//最大增量
#define MAXDELTA  \
	((256ul << ((sizeof(L->stack->tbclist.delta) - 1) * 8)) - 1)


/*
** Insert a variable in the list of to-be-closed variables.
*/

/// @brief 在要关闭的变量列表中插入一个变量
/// @param L 
/// @param level 
void luaF_newtbcupval (lua_State *L, StkId level) {
  lua_assert(level > L->tbclist);
  if (l_isfalse(s2v(level)))
    return;  /* false doesn't need to be closed */
  checkclosemth(L, level);  /* value must have a close method */
  while (cast_uint(level - L->tbclist) > MAXDELTA) {
    L->tbclist += MAXDELTA;  /* create a dummy node at maximum delta */
    L->tbclist->tbclist.delta = 0;
  }
  level->tbclist.delta = cast(unsigned short, level - L->tbclist);
  L->tbclist = level;
}

/// @brief 把当前UpVal从链表移除
/// @param uv 
void luaF_unlinkupval (UpVal *uv) {
  lua_assert(upisopen(uv));
  *uv->u.open.previous = uv->u.open.next;
  if (uv->u.open.next)
    uv->u.open.next->u.open.previous = uv->u.open.previous;
}


/*
** Close all upvalues up to the given stack level.
*/

/// @brief 
// UpValue 被特殊处理。因为 Lua 的 GC 可以分步扫描。别的类型被新创建时，
// 都可以直接作为一个白色节点（新节点）挂接在整个系统中。但 upvalue 却是对已有的对象的间接引用，不是新数据。
// 一旦 GC 在 mark 的过程中(gc 状态为 GCSpropagate)，则需增加屏障 luaC_barrier

// 其实这里可以发现函数没有关闭时，引用的内存还是openupval上，关闭后就重新赋值了一份，这个时候upval就不是共享的了，每个闭包一份了。
// 因为有upval的存在这个也是因为lua比较难正确性热更新的原因
/// @param L 
/// @param level 
void luaF_closeupval (lua_State *L, StkId level) {
  UpVal *uv;
  StkId upl;  /* stack index pointed by 'uv' */
  while ((uv = L->openupval) != NULL && (upl = uplevel(uv)) >= level) {
    TValue *slot = &uv->u.value;  /* new position for value */
    lua_assert(uplevel(uv) < L->top);
    luaF_unlinkupval(uv);  /* remove upvalue from 'openupval' list *///当前UpVal从L->openupval链表移除
    //值保存到v中 这个时候闭状态了，并不指向虚拟机的openupval中 这里内存就多了一份拷贝了，
    // 在虚拟机执行过程中，未关闭函数时，都指向同一份内存也就是在openupval中的内存 这里内存就多了一份拷贝了，
    // 在虚拟机执行过程中，未关闭函数时，都指向同一份内存也就是在openupval中的内存
    setobj(L, slot, uv->v);  /* move value to upvalue slot *////
    uv->v = slot;  /* now current value lives here *///uv->v指向close值
    if (!iswhite(uv)) {  /* neither white nor dead? *///如果不是白色
      nw2black(uv);  /* closed upvalues cannot be gray *///把它置成黑色
      luaC_barrier(L, uv, slot);//设置屏障
    }
  }
}


/*
** Remove firt element from the tbclist plus its dummy nodes.
*/

/// @brief 从tbclist删除一个元素
/// @param L 
static void poptbclist (lua_State *L) {
  StkId tbc = L->tbclist;
  lua_assert(tbc->tbclist.delta > 0);  /* first element cannot be dummy */
  tbc -= tbc->tbclist.delta;
  while (tbc > L->stack && tbc->tbclist.delta == 0)
    tbc -= MAXDELTA;  /* remove dummy nodes */
  L->tbclist = tbc;
}


/*
** Close all upvalues and to-be-closed variables up to the given stack
** level.
*/

/// @brief 
// 1. 根据level关闭移除对应的上值
// 2. 关闭不需要的upvalues以及to-be-closed的元素，对于to-be-closed的元素，还会调用其元方法。
/// @param L 
/// @param level 
/// @param status 
/// @param yy 
void luaF_close (lua_State *L, StkId level, int status, int yy) {
  ptrdiff_t levelrel = savestack(L, level);
  luaF_closeupval(L, level);  /* first, close the upvalues *///当前UpVal从L->openupval链表移除
  while (L->tbclist >= level) {  /* traverse tbc's down to that level */
    StkId tbc = L->tbclist;  /* get variable index */
    poptbclist(L);  /* remove it from list *///移除tbclist链表
    prepcallclosemth(L, tbc, status, yy);  /* close variable *///调用其元方法
    level = restorestack(L, levelrel);
  }
}

/// @brief 创建一个Proto结构体用于存放一个函数原型信息，并进行初始化 
/// @param L 
/// @return 
Proto *luaF_newproto (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VPROTO, sizeof(Proto));
  Proto *f = gco2p(o);
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->abslineinfo = NULL;
  f->sizeabslineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}

/// @brief 释放一个proto结构体
/// @param L 
/// @param f 
void luaF_freeproto (lua_State *L, Proto *f) {
  luaM_freearray(L, f->code, f->sizecode);
  luaM_freearray(L, f->p, f->sizep);
  luaM_freearray(L, f->k, f->sizek);
  luaM_freearray(L, f->lineinfo, f->sizelineinfo);
  luaM_freearray(L, f->abslineinfo, f->sizeabslineinfo);
  luaM_freearray(L, f->locvars, f->sizelocvars);
  luaM_freearray(L, f->upvalues, f->sizeupvalues);
  luaM_free(L, f);
}


/*
** Look for n-th local variable at line 'line' in function 'func'.
** Returns NULL if not found.
*/

/// @brief 获取局部变量的名字
/// @param f 
/// @param local_number 
/// @param pc 
/// @return 
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  /* not found */
}

