/*
** $Id: lopcodes.h $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "llimits.h"


/*===========================================================================
  We assume that instructions are unsigned 32-bit integers.
  All instructions have an opcode in the first 7 bits.
  Instructions can have the following formats:

        3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
        1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
iABC          C(8)     |      B(8)     |k|     A(8)      |   Op(7)     |
iABx                Bx(17)               |     A(8)      |   Op(7)     |
iAsBx              sBx (signed)(17)      |     A(8)      |   Op(7)     |
iAx                           Ax(25)                     |   Op(7)     |
isJ                           sJ(25)                     |   Op(7)     |

  A signed argument is represented in excess K: the represented value is
  the written unsigned value minus K, where K is half the maximum for the
  corresponding unsigned argument.
===========================================================================*/
// i:instruction 指令的意思
// A:指令1参数 一般用作目标寄存器索引
// B:指令2参数 既可以是寄存器索引，也可以是常量池索引
// C:指令3参数 既可以是寄存器索引，也可以是常量池索引
// k:一bit标志位 比如 标志位k为1表示常量池索引，否则表示寄存器索引
// x:extended 扩展的意思
// s:signed 符号 该参数应该被解释为有符号整数否则为无符号整数
// sJ:表示跳转的PC偏移量
// sBx和bx的区别是bx是一个无符号整数，而sbx表示的是一个有符号的数，也就是sbx可以是负数
enum OpMode {iABC, iABx, iAsBx, iAx, isJ};  /* basic instruction formats *///组合的类型


/*
** size and position of opcode arguments.
//操作码参数的大小和位置
*/
//----------------------指令各部分的大小 begin-------------------//
#define SIZE_C		8 //C参数大小8位
#define SIZE_B		8 //B参数大小8位
#define SIZE_Bx		(SIZE_C + SIZE_B + 1) //Bx17位
#define SIZE_A		8 //A参数大小8位
#define SIZE_Ax		(SIZE_Bx + SIZE_A) //Ax参数大小25位
#define SIZE_sJ		(SIZE_Bx + SIZE_A) //sJ参数大小25位

#define SIZE_OP		7 //OP参数大小7位
//----------------------指令各部分的大小 end-------------------//

//----------------------指令部分的起始位置 begin-------------------//
#define POS_OP		0 //初始起始位置0位

#define POS_A		(POS_OP + SIZE_OP)//A参数起始位置7位
#define POS_k		(POS_A + SIZE_A)//k参数起始位置15位
#define POS_B		(POS_k + 1)//B参数起始位置16位
#define POS_C		(POS_B + SIZE_B)//C参数起始位置7位

#define POS_Bx		POS_k//Bx参数起始位置15位

#define POS_Ax		POS_A//Ax参数起始位置7位

#define POS_sJ		POS_A//sJ参数起始位置7位
//----------------------指令部分的起始位置 end-------------------//

/*
** limits for opcode arguments.
** we use (signed) 'int' to manipulate most arguments,
** so they must fit in ints.
*/

/* Check whether type 'int' has at least 'b' bits ('b' < 32) */
///检测int类型必须有b位
#define L_INTHASBITS(b)		((UINT_MAX >> ((b) - 1)) >= 1)

//定义Ax、Bx、sBx、A、B、C部分所能存放的最大数值 
#if L_INTHASBITS(SIZE_Bx)
#define MAXARG_Bx	((1<<SIZE_Bx)-1) //2^17-1  Bx放的最大数值
#else
#define MAXARG_Bx	MAX_INT
#endif

#define OFFSET_sBx	(MAXARG_Bx>>1)         /* 'sBx' is signed *///2^16-1  sbx放的最大数值  有符号数


#if L_INTHASBITS(SIZE_Ax)
#define MAXARG_Ax	((1<<SIZE_Ax)-1) //2^25-1  Ax放的最大数值
#else
#define MAXARG_Ax	MAX_INT
#endif

#if L_INTHASBITS(SIZE_sJ)
#define MAXARG_sJ	((1 << SIZE_sJ) - 1)//2^25-1  sJ放的最大数值
#else
#define MAXARG_sJ	MAX_INT
#endif

#define OFFSET_sJ	(MAXARG_sJ >> 1)//2^24-1  sJ放的最大数值


#define MAXARG_A	((1<<SIZE_A)-1)//2^8-1  A放的最大数值
#define MAXARG_B	((1<<SIZE_B)-1)//2^8-1  B放的最大数值
#define MAXARG_C	((1<<SIZE_C)-1)//2^8-1  C放的最大数值
#define OFFSET_sC	(MAXARG_C >> 1)//2^7-1  sC放的最大数值

#define int2sC(i)	((i) + OFFSET_sC)//int转sC
#define sC2int(i)	((i) - OFFSET_sC)//sc转int


/* creates a mask with 'n' 1 bits at position 'p' */
// ~0 32位1 1111111111111111111111111111111111111111111111111111111111111111
// ~0<<1 最后一位变成0  1111111111111111111111111111111111111111111111111111111111111110
// ~(~0<<1) 全0最后一位为1 0001
// (~(~0<<1))<<1 第1位为1 其他位是0 第n位代表从后往前数 且从0开始 0010
// 这个宏的意义就是在第p位标记n个1 其余位是0
// 例如MASK1(2,3) 结果是11000
#define MASK1(n,p)	((~((~(Instruction)0)<<(n)))<<(p))

/* creates a mask with 'n' 0 bits at position 'p' */
// 这个宏的意义就是在第p位标记n个0 其余位是1
// 其实就是上面的宏取非
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/
//---------------------------------------用于操作指令的宏 begin------------------------------------------//
// get宏原理都是先将指令移位将要操作的值移动到最后再利用MASK1生成的结果进行与运算得到需要的值
// set宏原理 将需要设置值的位置清空 处理需要设置值为对应值其余位为0 将两者进行或运算
#define GET_OPCODE(i)	(cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))  //与上一个前7位的掩码
#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))// //把opcode位置为0,然后或上新的opcode

#define checkopm(i,m)	(getOpMode(GET_OPCODE(i)) == m)//检测指令的格式m是这个枚举值


//这两个宏和上面两个类似,只是偏移和大小变成了变量
#define getarg(i,pos,size)	(cast_int(((i)>>(pos)) & MASK1(size,0)))
#define setarg(i,v,pos,size)	((i) = (((i)&MASK0(size,pos)) | \
                ((cast(Instruction, v)<<pos)&MASK1(size,pos))))
//---------------------------------------用于操作指令的宏 end------------------------------------------//

//----------------------获取和设置指令各个部分的宏 begin----------------------------------//
#define GETARG_A(i)	getarg(i, POS_A, SIZE_A)//获取A参数的值
#define SETARG_A(i,v)	setarg(i, v, POS_A, SIZE_A)//设置A参数的值

#define GETARG_B(i)	check_exp(checkopm(i, iABC), getarg(i, POS_B, SIZE_B))//检测一下指令是不是iABC指令,并获取B参数的值
#define GETARG_sB(i)	sC2int(GETARG_B(i))//获取sB参数的值
#define SETARG_B(i,v)	setarg(i, v, POS_B, SIZE_B)//设置B参数的值

#define GETARG_C(i)	check_exp(checkopm(i, iABC), getarg(i, POS_C, SIZE_C))////检测一下指令是不是iABC指令,并获取C参数的值
#define GETARG_sC(i)	sC2int(GETARG_C(i))//获取sC参数的值
#define SETARG_C(i,v)	setarg(i, v, POS_C, SIZE_C)//设置C参数的值

#define TESTARG_k(i)	check_exp(checkopm(i, iABC), (cast_int(((i) & (1u << POS_k)))))//检测一下指令是不是iABC指令,并获取k参数的值是1还是0用于条件判断是常量池索引,还是寄存器索引
#define GETARG_k(i)	check_exp(checkopm(i, iABC), getarg(i, POS_k, 1))//检测一下指令是不是iABC指令,并获取k参数的值
#define SETARG_k(i,v)	setarg(i, v, POS_k, 1)//设置k参数的值

#define GETARG_Bx(i)	check_exp(checkopm(i, iABx), getarg(i, POS_Bx, SIZE_Bx))//检测一下指令是不是iABx指令,获取Bx参数的值
#define SETARG_Bx(i,v)	setarg(i, v, POS_Bx, SIZE_Bx)//设置Bx参数的值

#define GETARG_Ax(i)	check_exp(checkopm(i, iAx), getarg(i, POS_Ax, SIZE_Ax))//检测一下指令是不是iAx指令,获取Ax参数的值
#define SETARG_Ax(i,v)	setarg(i, v, POS_Ax, SIZE_Ax)//设置Ax参数的值

// sBx的表示范围 -(2^17-1) 到 2^17-1 是对称分布 有一个值没有利用 GETARG_Bx最小是0 所以sBx最小是-(2^17-1)
#define GETARG_sBx(i)  \
	check_exp(checkopm(i, iAsBx), getarg(i, POS_Bx, SIZE_Bx) - OFFSET_sBx)//检测一下指令是不是iAsBx指令,获取sBx参数的值
#define SETARG_sBx(i,b)	SETARG_Bx((i),cast_uint((b)+OFFSET_sBx))//设置sBx参数的值

#define GETARG_sJ(i)  \
	check_exp(checkopm(i, isJ), getarg(i, POS_sJ, SIZE_sJ) - OFFSET_sJ)//检测一下指令是不是isJ指令,获取sJ参数的值
#define SETARG_sJ(i,j) \
	setarg(i, cast_uint((j)+OFFSET_sJ), POS_sJ, SIZE_sJ)//设置sJ参数的值
//----------------------获取和设置指令各个部分的宏 end----------------------------------//

//----------------------指令创建 begin----------------------------------//
#define CREATE_ABCk(o,a,b,c,k)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, b)<<POS_B) \
			| (cast(Instruction, c)<<POS_C) \
			| (cast(Instruction, k)<<POS_k))//创建ABCK指令

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, bc)<<POS_Bx))//创建ABx指令

#define CREATE_Ax(o,a)		((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_Ax))//创建Ax指令

#define CREATE_sJ(o,j,k)	((cast(Instruction, o) << POS_OP) \
			| (cast(Instruction, j) << POS_sJ) \
			| (cast(Instruction, k) << POS_k))//创建sJ指令
//----------------------指令创建 end----------------------------------//

#if !defined(MAXINDEXRK)  /* (for debugging only) *///用于调试
#define MAXINDEXRK	MAXARG_B
#endif


/*
** invalid register that fits in 8 bits
*/

//标识是无效的8位寄存器
#define NO_REG		MAXARG_A


/*
** R(x) - register                                  一定是寄存器索引 一定要访问Lua栈
** Kst(x) - constant (in constant table)            一定是常量 在常量表中
** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x) 可能是常量也可能在Lua栈中
*/

/*
** Grep "ORDER OP" if you change these enums. Opcodes marked with a (*)
** has extra descriptions in the notes after the enumeration.
*/

/// @brief Lua虚拟机采用定长指令，每条指令占4个字节
// 在Lua 5.3里，操作码占指令低6位，因此总共能够容纳64条指令，定义了47条指令。
// Lua 5.4将操作码扩展到了7位，因此总共能够容纳128条指令，定义了83条
//立即数就是写在指令里的常数。用mov操作举例子，mov 12, %rax，那么这个 12 就在操作语句里。那么 12 相当于指令里的立即数。
typedef enum {
/*----------------------------------------------------------------------
  name		args	description
------------------------------------------------------------------------*/
/*赋值加载指令 begin*/
OP_MOVE,/*	A B	R[A] := R[B]					*///将B寄存器的值赋值给A寄存器
OP_LOADI,/*	A sBx	R[A] := sBx					*///加载整型立即数到寄存器
OP_LOADF,/*	A sBx	R[A] := (lua_Number)sBx				*///加载浮点立即数到寄存器
OP_LOADK,/*	A Bx	R[A] := K[Bx]					*///加载常量立即数到寄存器
OP_LOADKX,/*	A	R[A] := K[extra arg]				*///加载常量,常量从下一条OP_EXTRAARG指令得到
OP_LOADFALSE,/*	A	R[A] := false					*///加载false到寄存器
OP_LFALSESKIP,/*A	R[A] := false; pc++	(*)			*///加载false到寄存器,同时跳过下一条指令
OP_LOADTRUE,/*	A	R[A] := true					*///加载true到寄存器
OP_LOADNIL,/*	A B	R[A], R[A+1], ..., R[A+B] := nil		*///加载nil到一批寄存器
OP_GETUPVAL,/*	A B	R[A] := UpValue[B]				*/////读取一个上值到寄存器
OP_SETUPVAL,/*	A B	UpValue[B] := R[A]				*///写一个寄存器值到上值
/*赋值加载指令 end*/

/*表操作 begin*/
OP_GETTABUP,/*	A B C	R[A] := UpValue[B][K[C]:string]			*///从表取值到寄存器,标在upvalue
OP_GETTABLE,/*	A B C	R[A] := R[B][R[C]]				*///从表取值到寄存器
OP_GETI,/*	A B C	R[A] := R[B][C]					*///从表取整型字段值给寄存器
OP_GETFIELD,/*	A B C	R[A] := R[B][K[C]:string]			*///从表取字符串字段值给寄存器

OP_SETTABUP,/*	A B C	UpValue[A][K[B]:string] := RK(C)		*///设置寄存器值给表元素,表在upvalue
OP_SETTABLE,/*	A B C	R[A][R[B]] := RK(C)				*///设置寄存器值给标元素
OP_SETI,/*	A B C	R[A][B] := RK(C)				*///向表设置整型字段值
OP_SETFIELD,/*	A B C	R[A][K[B]:string] := RK(C)			*///向表设置字符串字段值

// 在R(A)位置新建一个表 参数B代表array部分大小 参数C代表hash部分大小
// 参数BC的表示方法查看luaO_int2fb
OP_NEWTABLE,/*	A B C k	R[A] := {}					*///新建一个表


OP_SELF,/*	A B C	R[A+1] := R[B]; R[A] := R[B][RK(C):string]	*///准备一个对象方法的调用
/*表操作 end*/


/*算术和位操作 begin*/
OP_ADDI,/*	A B sC	R[A] := R[B] + sC				*///立即数加

OP_ADDK,/*	A B C	R[A] := R[B] + K[C]:number			*///常量加
OP_SUBK,/*	A B C	R[A] := R[B] - K[C]:number			*///常量减
OP_MULK,/*	A B C	R[A] := R[B] * K[C]:number			*///常量乘
OP_MODK,/*	A B C	R[A] := R[B] % K[C]:number			*///常量模
OP_POWK,/*	A B C	R[A] := R[B] ^ K[C]:number			*///常量求幂
OP_DIVK,/*	A B C	R[A] := R[B] / K[C]:number			*///常量除
OP_IDIVK,/*	A B C	R[A] := R[B] // K[C]:number			*///常量整除

OP_BANDK,/*	A B C	R[A] := R[B] & K[C]:integer			*///常量与
OP_BORK,/*	A B C	R[A] := R[B] | K[C]:integer			*///常量或
OP_BXORK,/*	A B C	R[A] := R[B] ~ K[C]:integer			*///常量异或

OP_SHRI,/*	A B sC	R[A] := R[B] >> sC				*///立即数左移
OP_SHLI,/*	A B sC	R[A] := sC << R[B]				*///立即数右移

OP_ADD,/*	A B C	R[A] := R[B] + R[C]				*///加
OP_SUB,/*	A B C	R[A] := R[B] - R[C]				*///减
OP_MUL,/*	A B C	R[A] := R[B] * R[C]				*///乘
OP_MOD,/*	A B C	R[A] := R[B] % R[C]				*///模
OP_POW,/*	A B C	R[A] := R[B] ^ R[C]				*///幂
OP_DIV,/*	A B C	R[A] := R[B] / R[C]				*///浮点除
OP_IDIV,/*	A B C	R[A] := R[B] // R[C]				*///整除

OP_BAND,/*	A B C	R[A] := R[B] & R[C]				*///位与
OP_BOR,/*	A B C	R[A] := R[B] | R[C]				*///位或
OP_BXOR,/*	A B C	R[A] := R[B] ~ R[C]				*///位异或
OP_SHL,/*	A B C	R[A] := R[B] << R[C]				*///左移
OP_SHR,/*	A B C	R[A] := R[B] >> R[C]				*///右移

/*---------------对上一条失败的算术运算尝试元方法 begin*/
OP_MMBIN,/*	A B C	call C metamethod over R[A] and R[B]	(*)	*/
OP_MMBINI,/*	A sB C k	call C metamethod over R[A] and sB	*/
OP_MMBINK,/*	A B C k		call C metamethod over R[A] and K[B]	*/
/*---------------对上一条失败的算术运算尝试元方法 end*/

OP_UNM,/*	A B	R[A] := -R[B]					*///一元减
OP_BNOT,/*	A B	R[A] := ~R[B]					*///位非
/*算术和位操作 end*/

OP_NOT,/*	A B	R[A] := not R[B]				*///逻辑取反

/*其他操作 begin*/
OP_LEN,/*	A B	R[A] := #R[B] (length operator)			*///取长度
OP_CONCAT,/*	A B	R[A] := R[A].. ... ..R[A + B - 1]		*///拼接对象
/*其他操作 end*/

/*to-be-close变量 begin*/
OP_CLOSE,/*	A	close all upvalues >= R[A]			*///关闭tbc
OP_TBC,/*	A	mark variable A "to be closed"			*///标记寄存器为tbc
/*to-be-close变量 end*/

/*逻辑判断和跳转 begin*/
OP_JMP,/*	sJ	pc += sJ					*///无条件跳转
OP_EQ,/*	A B k	if ((R[A] == R[B]) ~= k) then pc++		*///相等测试,条件跳转
OP_LT,/*	A B k	if ((R[A] <  R[B]) ~= k) then pc++		*///小于测试,条件跳转
OP_LE,/*	A B k	if ((R[A] <= R[B]) ~= k) then pc++		*///小于等于测试,条件跳转

OP_EQK,/*	A B k	if ((R[A] == K[B]) ~= k) then pc++		*///常量相等测试,条件跳转
OP_EQI,/*	A sB k	if ((R[A] == sB) ~= k) then pc++		*///立即数相等测试,条件跳转
OP_LTI,/*	A sB k	if ((R[A] < sB) ~= k) then pc++			*///立即数小于测试,条件跳转
OP_LEI,/*	A sB k	if ((R[A] <= sB) ~= k) then pc++		*///立即数小于等于测试,条件跳转
OP_GTI,/*	A sB k	if ((R[A] > sB) ~= k) then pc++			*///立即数大于测试,条件跳转
OP_GEI,/*	A sB k	if ((R[A] >= sB) ~= k) then pc++		*///立即数大于等于测试,条件跳转

OP_TEST,/*	A k	if (not R[A] == k) then pc++			*///bool测试,条件跳转
OP_TESTSET,/*	A B k	if (not R[B] == k) then pc++ else R[A] := R[B] (*) *///bool测试,条件跳转和赋值
/*逻辑判断和跳转 end*/

/*函数调用 begin*/
OP_CALL,/*	A B C	R[A], ... ,R[A+C-2] := R[A](R[A+1], ... ,R[A+B-1]) *///函数调用 
OP_TAILCALL,/*	A B C k	return R[A](R[A+1], ... ,R[A+B-1])		*///尾调用
OP_RETURN,/*	A B C k	return R[A], ... ,R[A+B-2]	(see note)	*///从函数调用返回
OP_RETURN0,/*		return						*///返回无结果
OP_RETURN1,/*	A	return R[A]					*///返回一个参数
/*函数调用 end*/

/*for 循环 begin*/
OP_FORLOOP,/*	A Bx	update counters; if loop continues then pc-=Bx; *///数值for循环
OP_FORPREP,/*	A Bx	<check values and prepare counters>;
                        if not to run then pc+=Bx+1;			*///数值for循环

OP_TFORPREP,/*	A Bx	create upvalue for R[A + 3]; pc+=Bx		*///通用for循环
OP_TFORCALL,/*	A C	R[A+4], ... ,R[A+3+C] := R[A](R[A+1], R[A+2]);	*///通用for循环
OP_TFORLOOP,/*	A Bx	if R[A+2] ~= nil then { R[A]=R[A+2]; pc -= Bx }	*///通用for循环
/*for 循环 end*/

/*表操作 begin*/
OP_SETLIST,/*	A B C k	R[A][C+i] := R[A+i], 1 <= i <= B		*///给表设置一批数组元素
/*表操作 end*/

/*函数调用 begin*/
OP_CLOSURE,/*	A Bx	R[A] := closure(KPROTO[Bx])			*///根据函数原型新建一个闭包

OP_VARARG,/*	A C	R[A], R[A+1], ..., R[A+C-2] = vararg		*///将函数的可变参数拷贝给寄存器

OP_VARARGPREP,/*A	(adjust vararg parameters)			*///跳转可变函数的调用信息
/*函数调用 end*/

/*附件参数 begin*/
OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*///为上一条指令提供额外参数
/*附件参数 end*/
} OpCode;


#define NUM_OPCODES	((int)(OP_EXTRAARG) + 1)//指令数量



/*===========================================================================
  Notes:

  (*) Opcode OP_LFALSESKIP is used to convert a condition to a boolean
  value, in a code equivalent to (not cond ? false : true).  (It
  produces false and skips the next instruction producing true.)

  (*) Opcodes OP_MMBIN and variants follow each arithmetic and
  bitwise opcode. If the operation succeeds, it skips this next
  opcode. Otherwise, this opcode calls the corresponding metamethod.

  (*) Opcode OP_TESTSET is used in short-circuit expressions that need
  both to jump and to produce a value, such as (a = b or c).

  (*) In OP_CALL, if (B == 0) then B = top - A. If (C == 0), then
  'top' is set to last_result+1, so next open instruction (OP_CALL,
  OP_RETURN*, OP_SETLIST) may use 'top'.

  (*) In OP_VARARG, if (C == 0) then use actual number of varargs and
  set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to 'top'.

  (*) In OP_LOADKX and OP_NEWTABLE, the next instruction is always
  OP_EXTRAARG.

  (*) In OP_SETLIST, if (B == 0) then real B = 'top'; if k, then
  real C = EXTRAARG _ C (the bits of EXTRAARG concatenated with the
  bits of C).

  (*) In OP_NEWTABLE, B is log2 of the hash size (which is always a
  power of 2) plus 1, or zero for size zero. If not k, the array size
  is C. Otherwise, the array size is EXTRAARG _ C.

  (*) For comparisons, k specifies what condition the test should accept
  (true or false).

  (*) In OP_MMBINI/OP_MMBINK, k means the arguments were flipped
   (the constant is the first operand).

  (*) All 'skips' (pc++) assume that next instruction is a jump.

  (*) In instructions OP_RETURN/OP_TAILCALL, 'k' specifies that the
  function builds upvalues, which may need to be closed. C > 0 means
  the function is vararg, so that its 'func' must be corrected before
  returning; in this case, (C - 1) is its number of fixed parameters.

  (*) In comparisons with an immediate operand, C signals whether the
  original operand was a float. (It must be corrected in case of
  metamethods.)

===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-2: op mode
** bit 3: instruction set register A
** bit 4: operator is a test (next instruction must be a jump)
** bit 5: instruction uses 'L->top' set by previous instruction (when B == 0)
** bit 6: instruction sets 'L->top' for next instruction (when C == 0)
** bit 7: instruction is an MM instruction (call a metamethod)
*/
// ** 指令属性的掩码
// ** 位0-2：指令的类型 也就是这些 {iABC, iABx, iAsBx, iAx, isJ}
// ** 位3：指令是否修改了寄存器A 
// ** 位4：指令是否是测试指令下一条指令一定是jump指令 
// ** 位5：使用前一条指令设置的L->top的值（当 B == 0 时） 
// ** 位6：设置L->Top用于下一条指令（当C == 0时） 
// ** 位7：指令是MM指令（调用元方法）

LUAI_DDEC(const lu_byte luaP_opmodes[NUM_OPCODES];)

#define getOpMode(m)	(cast(enum OpMode, luaP_opmodes[m] & 7))//获取指令的类型
#define testAMode(m)	(luaP_opmodes[m] & (1 << 3))//检查指令是否修改A寄存器
#define testTMode(m)	(luaP_opmodes[m] & (1 << 4))//检查指令是否是测试指令
#define testITMode(m)	(luaP_opmodes[m] & (1 << 5))//检查指令是否可以使用前一条指令设置的L->top的值
#define testOTMode(m)	(luaP_opmodes[m] & (1 << 6))//检查指令是否可以设置L->Top 用于下一条指令
#define testMMMode(m)	(luaP_opmodes[m] & (1 << 7))//检查指令是否元方法指令

/* "out top" (set top for next instruction) */
//设置L->Top用于下一条指令
#define isOT(i)  \
	((testOTMode(GET_OPCODE(i)) && GETARG_C(i) == 0) || \
          GET_OPCODE(i) == OP_TAILCALL)

/* "in top" (uses top from previous instruction) */
//使用前一条指令设置的L->top的值
#define isIT(i)		(testITMode(GET_OPCODE(i)) && GETARG_B(i) == 0)

//// opmode对应相应操作模式的一个映射表
// mm:元方法
// ot:设置L->Top用于下一条指令
// it:使用前一条指令设置的L->top的值
// t:是否是测试指令
// a:是否修改A寄存器
// m:指令的类型
#define opmode(mm,ot,it,t,a,m)  \
    (((mm) << 7) | ((ot) << 6) | ((it) << 5) | ((t) << 4) | ((a) << 3) | (m))


/* number of list items to accumulate before a SETLIST instruction */
// SETLIST指令之前积累的list项的个数
#define LFIELDS_PER_FLUSH	50

#endif
