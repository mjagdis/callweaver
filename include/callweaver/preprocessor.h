/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Preprocessor macros
 *
 * The increment and iteration macros support a fixed maximum
 * value / iteration count. If you need more you have to copy,
 * paste and amend to add more definitions of CW_CPP_INC_<n>
 * and CW_CPP_ITERATE_<n>.
 *
 * If you are wondering why we have to paste the iteration number
 * on to a token and then define a separate macro for each and
 * every possible iteration, well, preprocessor expansion lacks
 * conditionals, control structures, loops and just about anything
 * else useful. Look, it's just not intended to do this. We're
 * a hair's breadth away from needing to use "something else" to
 * implement a preprocessing phase before we get anywhere near
 * the C/C++ compiler...
 *
 * If you _really_ feel the need to grok the preprocessor try
 * reading the preprocessor section of http://www.boost.org/.
 *
 */
#ifndef _CALLWEAVER_PREPROCESSOR_H
#define _CALLWEAVER_PREPROCESSOR_H

#define CW_CPP_CAT(a, b)	a ## b
#define CW_CPP_DEBRACKET(...)	__VA_ARGS__
#define CW_CPP_DO(op, ...)	op(__VA_ARGS__)

#define CW_CPP_INC_0	1
#define CW_CPP_INC_1	2
#define CW_CPP_INC_2	3
#define CW_CPP_INC_3	4
#define CW_CPP_INC_4	5
#define CW_CPP_INC_5	6
#define CW_CPP_INC_6	7
#define CW_CPP_INC_7	8
#define CW_CPP_INC_8	9
#define CW_CPP_INC_9	10
#define CW_CPP_INC_10	11
#define CW_CPP_INC_11	12
#define CW_CPP_INC_12	13
#define CW_CPP_INC_13	14
#define CW_CPP_INC_14	15
#define CW_CPP_INC_15	16
#define CW_CPP_INC_16	17
#define CW_CPP_INC_17	18
#define CW_CPP_INC_18	19
#define CW_CPP_INC_19	20
#define CW_CPP_INC_20	21
#define CW_CPP_INC_21	22
#define CW_CPP_INC_22	23
#define CW_CPP_INC_23	24
#define CW_CPP_INC_24	25
#define CW_CPP_INC_25	26
#define CW_CPP_INC_26	27
#define CW_CPP_INC_27	28
#define CW_CPP_INC_28	29
#define CW_CPP_INC_29	30
#define CW_CPP_INC_30	31

#define CW_CPP_INC(n)	CW_CPP_CAT(CW_CPP_INC_, n)


#define CW_CPP_ITERATE_1(n, op, a)		CW_CPP_DO(op, n, a)
#define CW_CPP_ITERATE_2(n, op, a, b)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 1)(CW_CPP_CAT(CW_CPP_INC_, n), op, b)
#define CW_CPP_ITERATE_3(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 2)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_4(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 3)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_5(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 4)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_6(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 5)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_7(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 6)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_8(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 7)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_9(n, op, a, ...)		CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 8)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_10(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 9)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_11(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 10)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_12(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 11)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_13(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 12)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_14(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 13)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_15(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 14)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_16(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 15)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_17(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 16)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_18(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 17)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_19(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 18)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_20(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 19)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_21(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 20)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_22(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 21)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_23(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 22)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_24(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 23)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_25(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 24)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_26(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 25)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_27(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 26)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_28(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 27)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_29(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 28)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)
#define CW_CPP_ITERATE_30(n, op, a, ...)	CW_CPP_CAT(CW_CPP_ITERATE_, 1)(n, op, a) CW_CPP_CAT(CW_CPP_ITERATE_, 29)(CW_CPP_CAT(CW_CPP_INC_, n), op, __VA_ARGS__)

#endif /* _CALLWEAVER_PREPROCESSOR_H */
