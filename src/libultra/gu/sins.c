/**************************************************************************
 *									  *
 *		 Copyright (C) 1994, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include "guint.h"

/* ====================================================================
 * ====================================================================
 *
 * Module: sins.c
 * $Revision: 1.3 $
 * $Date: 1998/10/09 06:15:00 $
 * $Author: has $
 * $Source: /exdisk2/cvs/N64OS/Master/cvsmdev2/PR/libultra/monegi/gu/sins.c,v $
 *
 * ====================================================================
 * ====================================================================
 */

/* coefficients for polynomial approximation of sin on +/- pi/2 */

/* ====================================================================
 *
 * FunctionName		sins
 *
 * Description		computes sine of arg
 *
 * ====================================================================
 */

#include "sintable.h"