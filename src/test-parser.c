#include <glib.h>
#include <stdio.h>
#include "numbers.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"

char *exp [] = {
	"1+2",
	"1.5+1.2",
	"1.0*5.3",
	"2*4.1",
	"4.1*3",
	"5/0",
	"10.0/0",
	"a1+1",
	"$a1+1",
	"a$1+1",
	"$a$1+1",
	NULL
};

int
main ()
{
	Value *v;
	EvalTree *node;
	int i;
	char *error;
	
	for (i = 0; exp [i]; i++){
		printf ("Expression: %s;  ", exp [i]);
		node = expr_parse_string (exp [i], 0, 0, NULL, &error);
		if (node == NULL){
			printf ("parse error: %s\n", error);
			continue;
		}
		v = eval_expr (NULL, node, 0, 0, &error);
		if (v == NULL){
			printf ("eval error: %s\n", error);
			continue;
		}
		value_dump (v);
	}
	
	return 0;
}
