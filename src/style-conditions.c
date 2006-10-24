/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * style-conditions.c: 
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "style-conditions.h"
#include "mstyle.h"
#include "gnm-style-impl.h"
#include "expr.h"
#include "cell.h"
#include "value.h"
#include "sheet.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>

#define BLANKS_STRING_FOR_MATCHING " \t\n\r"

typedef GObjectClass GnmStyleConditionsClass;
struct _GnmStyleConditions {
	GObject base;
	GArray *conditions;
};

static GObjectClass *parent_class;

static gboolean
cond_validate (GnmStyleCond const *cond)
{
	g_return_val_if_fail (cond != NULL, FALSE);
	g_return_val_if_fail (cond->overlay != NULL, FALSE);
	g_return_val_if_fail (cond->texpr[0] != NULL, FALSE);
	g_return_val_if_fail ((cond->op == GNM_STYLE_COND_BETWEEN ||
			   cond->op == GNM_STYLE_COND_NOT_BETWEEN) ^
			  (cond->texpr[1] == NULL), FALSE);

	return TRUE;
}
	
static void
cond_unref (GnmStyleCond const *cond)
{
	/* Be very delicate, this is called for invalid conditions too */
	if (cond->overlay)
		gnm_style_unref (cond->overlay);
	if (cond->texpr[0])
		gnm_expr_top_unref (cond->texpr[0]);
	if (cond->texpr[1])
		gnm_expr_top_unref (cond->texpr[1]);
}

static void
gnm_style_conditions_finalize (GObject *obj)
{
	GnmStyleConditions *sc = (GnmStyleConditions *)obj;

	if (sc->conditions != NULL) {
		int i = sc->conditions->len;
		while (i-- > 0)
			cond_unref (&g_array_index (sc->conditions, GnmStyleCond, i));
		g_array_free (sc->conditions, TRUE);
		sc->conditions = NULL;
	}
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}
static void
gnm_style_conditions_init (GnmStyleConditions *sc)
{
	sc->conditions = NULL;
}

static void
gnm_style_conditions_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);
	gobject_class->finalize         = gnm_style_conditions_finalize;
}
static GSF_CLASS (GnmStyleConditions, gnm_style_conditions,
		  gnm_style_conditions_class_init, gnm_style_conditions_init,
		  G_TYPE_OBJECT)

/**
 * gnm_style_conditions_new :
 * 
 * Convenience tool to create a GnmStyleCondition.  straight g_object_new will work too.
 *
 * Returns a GnmStyleConditions that the caller is resoinsible for.
 **/
GnmStyleConditions  *
gnm_style_conditions_new (void)
{
	return g_object_new (gnm_style_conditions_get_type (), NULL);
}

/**
 * gnm_style_conditions_details :
 * @sc : #GnmStyleConditions
 *
 * Returns an array of GnmStyleCond which should not be modified.
 **/
GArray const	*
gnm_style_conditions_details (GnmStyleConditions const *sc)
{
	g_return_val_if_fail (sc != NULL, NULL);

	return sc->conditions;
}

/**
 * gnm_style_conditions_insert :
 * @sc : #GnmStyleConditions
 * @cond : #GnmStyleCond
 * @pos : position.
 *
 * Insert @cond before @pos (append if @pos < 0).
 **/
void
gnm_style_conditions_insert (GnmStyleConditions *sc,
			     GnmStyleCond const *cond, int pos)
{
	g_return_if_fail (cond != NULL);

	if (sc == NULL || !cond_validate (cond)) {
		cond_unref (cond); /* be careful not to leak */
		return;
	}

	if (sc->conditions == NULL)
		sc->conditions = g_array_new (FALSE, FALSE, sizeof (GnmStyleCond));

	if (pos < 0)
		g_array_append_val (sc->conditions, *cond);
	else
		g_array_insert_val (sc->conditions, pos, *cond);
}

GPtrArray *
gnm_style_conditions_overlay (GnmStyleConditions const *sc,
			      GnmStyle const *base)
{
	GPtrArray *res;
	GnmStyle const *overlay;
	GnmStyle *merge;
	unsigned i;

	g_return_val_if_fail (sc != NULL, NULL);
	g_return_val_if_fail (sc->conditions != NULL, NULL);

	res = g_ptr_array_sized_new (sc->conditions->len);
	for (i = 0 ; i < sc->conditions->len; i++) {
		overlay = g_array_index (sc->conditions, GnmStyleCond, i).overlay;
		merge = gnm_style_new_merged (base, overlay);
		/* We only draw a background colour is the pattern != 0 */
		if (merge->pattern == 0 &&
		     elem_is_set (overlay, MSTYLE_COLOR_BACK) &&
		    !elem_is_set (overlay, MSTYLE_PATTERN))
			merge->pattern = 1;
		g_ptr_array_add (res, merge);
	}
	return res;
}

#include <parse-util.h>
int
gnm_style_conditions_eval (GnmStyleConditions const *sc, GnmEvalPos const *ep)
{
	unsigned i;
	gboolean use_this = FALSE;
	GnmValue *val;
	GArray const *conds;
	GnmStyleCond const *cond;
	GnmParsePos pp;
	GnmCell const *cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	GnmValue const *cv = cell ? cell->value : NULL;
	/*We should really assert that cv is not NULL, but asserts are apparently frowned upon.*/

	g_return_val_if_fail (sc != NULL, -1);
	g_return_val_if_fail (sc->conditions != NULL, -1);

	conds = sc->conditions;
	parse_pos_init_evalpos (&pp, ep);
	for (i = 0 ; i < conds->len ; i++) {
		cond = &g_array_index (conds, GnmStyleCond, i);

		val = gnm_expr_top_eval (cond->texpr[0], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		if (cond->op == GNM_STYLE_COND_CUSTOM) {
			use_this = value_get_as_bool (val, NULL);
#if 0
			char *str = gnm_expr_as_string (cond->expr[0],
				&pp, gnm_expr_conventions_default);
			g_print ("'%s' = %s\n", str, use_this ? "true" : "false");
			g_free (str);
#endif
		} else if (cond->op < GNM_STYLE_COND_CONTAINS_STR) {
			GnmValDiff diff = value_compare (cv, val, TRUE);

			switch (cond->op) {
			default:
			case GNM_STYLE_COND_EQUAL:	use_this = (diff == IS_EQUAL); break;
			case GNM_STYLE_COND_NOT_EQUAL:	use_this = (diff != IS_EQUAL); break;
			case GNM_STYLE_COND_NOT_BETWEEN:
				if (diff != IS_LESS)
					break;
				value_release (val);
				val = gnm_expr_top_eval (cond->texpr[1], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
				diff = value_compare (cv, val, TRUE);
				/* fall through */

			case GNM_STYLE_COND_GT:		use_this = (diff == IS_GREATER); break;
			case GNM_STYLE_COND_LT:		use_this = (diff == IS_LESS); break;
			case GNM_STYLE_COND_GTE:	use_this = (diff != IS_LESS); break;
			case GNM_STYLE_COND_BETWEEN:
				if (diff == IS_LESS)
					break;
				value_release (val);
				val = gnm_expr_top_eval (cond->texpr[1], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
				diff = value_compare (cv, val, TRUE);
				/* fall through */
			case GNM_STYLE_COND_LTE:	use_this = (diff != IS_GREATER); break;
			}
		} else {
			char const* valstring;
			char* ptr;
			char* ptr2;
			char const* cvstring;
			int slen;
			/*Two cases treated here do not need val as a string.*/
			if(!((cond->op == GNM_STYLE_COND_CONTAINS_ERR) || (cond->op == GNM_STYLE_COND_NOT_CONTAINS_ERR))) {
				valstring = value_peek_string (val);
				cvstring = value_peek_string (cv);
				slen = strlen(cvstring);
			}else{
				/*"Safe" values.  If we hosed
				 *something, we will know about it*/
				valstring = NULL;
				cvstring = NULL;
				slen = -1;
			}
			/*We need to know the length of the string for most cases*/
			/*Make sure the inputs are both strings.
			 *UPDATE: removed the valstring string check, as it only slows
			 *  stuff down.  VALUE MUST BE A STRING!*/
			if((!(VALUE_IS_STRING(cv))) && (cond->op != GNM_STYLE_COND_CONTAINS_ERR) && (cond->op != GNM_STYLE_COND_NOT_CONTAINS_ERR)) {
				/*Error: one of 'em is not a string.  Obviously fails the test.*/
				use_this = 0;
			}else{
				switch (cond->op) {
				default :
					/*This really should be an error, I suspect*/
					break;
				case GNM_STYLE_COND_CONTAINS_STR :
					use_this = (int) strstr (cvstring,valstring);
					break;
				case GNM_STYLE_COND_NOT_CONTAINS_STR :
					use_this = ! (int) strstr (cvstring,valstring);
					break;
				case GNM_STYLE_COND_BEGINS_WITH_STR :
					use_this = ! (strncmp (cvstring, valstring, slen));
					break;
				case GNM_STYLE_COND_NOT_BEGINS_WITH_STR :
					use_this = strncmp (cvstring, valstring, slen);
					break;
				case GNM_STYLE_COND_ENDS_WITH_STR :
					/*gedanken experiment: valuestr="foofoo"; cvstring="foo"
					 * This loop solves the problem.
					 *First, make sure ptr2 is NULL in case it never gets assigned*/
					ptr2 = NULL;
					while ((ptr = strstr (cvstring,valstring))) {
						/*ptr2 will point to the beginning of the last match*/
						ptr2 = ptr;
					}
					/*If it matches; is it the *end* match?*/
					/*If so, using strcmp with the pointer to the position of the
					 *  match will return 0 (lexically equal)*/
					if (ptr2) {
						/*We compare the *whole* string:
						 * look for "bar" at the end of "foobarbaz"
						 */
						use_this = ! (int) strcmp (ptr2, valstring);
					} else {
						/*don't match if no match was found.  duh.*/
						use_this = 0;
					}
					break;
				case GNM_STYLE_COND_NOT_ENDS_WITH_STR :
					/*gedanken experiment: valuestr="foofoo"; cvstring="foo"
					 * This loop solves the problem.
					 *First, make sure ptr2 is NULL in case it never gets assigned*/
					ptr2 = NULL;
					while ((ptr = strstr (cvstring,valstring))) {
						/*ptr2 will point to the beginning of the last match*/
						ptr2 = ptr;
					}
					/*If it matches; is it the *end* match?*/
					/*If so, using strcmp with the pointer to the position of the
					 *  match will return 0 (lexically equal)*/
					if (ptr2) {
						/*If strcmp returns zero, then cvstring was at the end
						 *  of this string, and obvioulsy the string does not
						 *  not-end with cvstring.  Else it didn't and so it
						 *  does.  :)*/
						use_this = (int) strcmp (ptr2, valstring);
					} else {
						/*No match was found.  We do not-end with cvstring.*/
						use_this = 1;
					}
					break;
				case GNM_STYLE_COND_CONTAINS_ERR :
					use_this = VALUE_IS_ERROR (cv);
					break;
				case GNM_STYLE_COND_NOT_CONTAINS_ERR :
					use_this = ! (VALUE_IS_ERROR (cv));
					break;
				case GNM_STYLE_COND_CONTAINS_BLANKS :
					use_this = (int) strpbrk (cvstring, BLANKS_STRING_FOR_MATCHING);
					break;
				case GNM_STYLE_COND_NOT_CONTAINS_BLANKS :
					use_this = ! (int) strpbrk (cvstring, BLANKS_STRING_FOR_MATCHING);
					break;
				}
			}
		}

		value_release (val);
		if (use_this)
			return i;
	}
	return -1;
}
