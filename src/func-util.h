#ifndef GNUMERIC_FUNC_UTIL_H
#define GNUMERIC_FUNC_UTIL_H

#include "numbers.h"

typedef struct {
	int N;
	float_t M, Q, sum;
        gboolean afun_flag;
} stat_closure_t;

void setup_stat_closure     (stat_closure_t *cl);
Value *callback_function_stat (const EvalPos *ep, Value *value,
			       void *closure);

Value *gnumeric_average     (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_count       (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_stdev       (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_stdevp      (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_var         (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_varp        (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_counta      (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_min         (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_max         (FunctionEvalInfo *s, GList *nodes);

Value *gnumeric_return_current_time (void);


/* Type definitions and function prototypes for criteria functions.
 * This includes the database functions and some mathematical functions
 * like COUNTIF, SUMIF...
 */
typedef int (*criteria_test_fun_t) (Value *x, Value *y);

typedef struct {
        criteria_test_fun_t fun;
        Value               *x;
        int                 column;
} func_criteria_t;

int  criteria_test_equal            (Value *x, Value *y);
int  criteria_test_unequal          (Value *x, Value *y);
int  criteria_test_greater          (Value *x, Value *y);
int  criteria_test_less             (Value *x, Value *y);
int  criteria_test_greater_or_equal (Value *x, Value *y);
int  criteria_test_less_or_equal    (Value *x, Value *y);
void parse_criteria                 (const char *criteria,
				     criteria_test_fun_t *fun,
				     Value **test_value);
GSList *parse_criteria_range        (Sheet *sheet, int b_col, int b_row,
				     int e_col, int e_row,
				     int   *field_ind);
void free_criterias                 (GSList *criterias);
GSList *find_rows_that_match        (Sheet *sheet, int first_col,
				     int first_row, int last_col, int last_row,
				     GSList *criterias, gboolean unique_only);

#endif /* GNUMERIC_FUNC_UTIL_H */
