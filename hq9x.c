
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char HQ9X_VERSION[] = "EHQI 0.9.2 - An extensible HQ9+ interpreter\n";

static void * clear_alloc(size_t size)
{
	void * ptr = malloc(size);
	memset(ptr, 0, size);
	return ptr;
}

static char * strdupto(const char * text, size_t size)
{
	char * result;
	size = strnlen(text, size);
	result = malloc(size + 1);
	memcpy(result, text, size);
	result[size] = '\0';
	return result;
}

static char * readall(FILE * input)
{
	size_t size, count;
	char * buff;
	char * end;
	end = buff = malloc(1 + (size = 16));

	while((count = fread(end, 1, 16, input)) == 16)
	{
		buff = realloc(buff, 1 + (size += 16));
		end += 16;
	}

	buff[size - 16 + count] = '\0';

	return realloc(buff, 1 + size - 16 + count);
}

static char ** getlines(char * text, size_t * countp)
{
	char ** arr;
	char * ptr = NULL;
	char * last;
	size_t size = 1, count = 0;
	while((ptr = strchr(ptr ? ptr + 1 : text, '\n')))
		size += 1;
	if(countp)
		*countp = size;
	arr = malloc((size + 1) * sizeof(char *));
	last = text;
	while((ptr = strchr(last, '\n')))
	{
		arr[count++] = strdupto(last, ptr - last);
		last = ptr + 1;
	}
	arr[count++] = strdup(last);
	arr[count] = NULL;
	return arr;
}

typedef struct hq9x_state hq9x_state_t;
typedef void (*function_ptr_t)(hq9x_state_t *);

/* Smart source control */

typedef struct source_t
{
	FILE * file;

	char * text; /* the whole text in one string */
	char * pointer; /* a pointer into either the text or the current line */

	char ** lines; /* a null-terminated array of strings that are the lines */
	size_t count; /* size of the array */

	char ** line; /* the current line */
	int dir, out_of_bound; /* the direction, and whether the pointer is out of bounds */

	char * last_nl; /* last newline, for easier change from freeform to grid-like control */
} source_t;

static void source_init(source_t * source, FILE * file)
{
	memset(source, 0, sizeof(source_t));
	source->file = stdin;
	source->dir = '>';
}

static void source_free(source_t * source)
{
	if(source->text)
		free(source->text);
	if(source->lines)
	{
		char ** current;
		for(current = source->lines; *current; current++)
			free(*current);
		free(source->lines);
	}
}

static char * source_get_text(source_t * source)
{
	if(!source->text)
		source->text = readall(source->file);
	return source->text;
}

static char * source_get_pointer(source_t * source)
{
	if(!source->pointer)
	{
		if(source->lines)
			source->pointer = source->lines[0];
		else
			source->pointer = source_get_text(source);
	}
	return source->pointer;
}

static char * source_get_line(source_t * source)
{
	return source->line ? *source->line : /*source->last_nl ? source->last_nl + 1 :*/ source->text;
	/* if not parsed into lines, give back character after last newline, otherwise give whole text */
}

static char ** source_get_lines(source_t * source)
{
	if(!source->lines)
	{
		source->lines = getlines(source_get_text(source), &source->count);
		/* since the text is cut into lines, the pointer must point into the first line instead of the whole text */
		source->line = &source->lines[0];
		if(source->pointer)
			source->pointer += *source->line - (source->last_nl ? source->last_nl + 1 : source->text);
	}
	return source->lines;
}

static void source_ensure_line(source_t * source, int lineno, int pos)
{
	source_get_lines(source);
	if(source->count < lineno)
	{
		int tmp = source->line - source->lines;
		int i;
		source->lines = realloc(source->lines, (lineno + 2) * sizeof(char *));
		for(i = source->count; i < lineno + 1; i++)
			source->lines[i] = strdup("");
		source->count = lineno + 1;
		source->lines[source->count] = NULL;
		source->line = source->lines + tmp;
	}

	if(strlen(source->lines[lineno]) < pos)
	{
		size_t size = strlen(source->lines[lineno]);
		int tmp = source->pointer - *source->line;
		int iscurrent = source->line == &source->lines[lineno];
		source->lines[lineno] = realloc(source->line[lineno], pos + 2);
		memset(source->lines[lineno] + size, ' ', pos + 1 - size);
		source->lines[lineno][pos + 1] = '\0';
		if(iscurrent)
		{
			source->line = &source->lines[lineno];
			source->pointer = &(*source->line)[tmp];
		}
	}
}

#define BEFUNGE_WRAPPING 1

static void source_advance(source_t * source)
{
	if(!source->lines && source->dir == '>')
	{
		if(!source->lines && *source_get_pointer(source) == '\n')
			source->last_nl = source_get_pointer(source);
		if(source_get_pointer(source)[0] && source_get_pointer(source)[1])
			source->pointer ++;
		else
			source->out_of_bound = 1;
		return;
	}
	else if(!source->lines)
	{
		source_get_lines(source);
	}

	switch(source->dir)
	{
	case '>':
		if(source_get_pointer(source)[0] && source_get_pointer(source)[1])
			source->pointer ++;
		else if(BEFUNGE_WRAPPING)
			source->pointer = source_get_line(source);
		else
			source->out_of_bound = 1;
	break;
	case '<':
		if(source_get_pointer(source) != source_get_line(source))
			source->pointer --;
		else if(BEFUNGE_WRAPPING)
			source->pointer = source->line[0] + strlen(source->line[0]);
		else
			source->out_of_bound = 1;
	break;
	case 'v':
		if(source->line && source->line[1])
		{
			source->pointer += source->line[1] - source->line[0];
			source->line ++;
		}
		else if(BEFUNGE_WRAPPING)
		{
			source->pointer += source->lines[0] - source->line[0];
			source->line = &source->lines[0];
		}
		else
			source->out_of_bound = 1;
		source_ensure_line(source, 0, source->pointer - source->line[0]);
	break;
	case '^':
		if(source->line && source->line != source->lines)
		{
			source->pointer += source->line[-1] - source->line[0];
			source->line --;
		}
		else if(BEFUNGE_WRAPPING)
		{
			source->pointer += source->lines[source->count - 1] - source->line[0];
			source->line = &source->lines[source->count - 1];
		}
		else
			source->out_of_bound = 1;
		source_ensure_line(source, source->count - 1, source->pointer - source->line[0]);
	break;
	}
}

static void source_ensure_grid(source_t * source, int height, int width)
{
	int i;
	source_get_lines(source);
	for(i = height - 1; i >= 0; i--)
		source_ensure_line(source, i, width - 1);
}

/* The BF interpreter state */

typedef char bf_cell_t;
typedef struct bf_state
{
	int enabled;
	bf_cell_t * cells;
	size_t count;
	size_t pointer;
} bf_state_t;

/* The Befunge interpreter state */

typedef long bef_cell_t;
typedef struct bef_state
{
	int enabled;
	bef_cell_t * stack;
	size_t capacity;
	size_t pointer;
	int stringmode;
} bef_state_t;

/* OO extensions */

typedef struct hq9x_class hq9x_class_t;
typedef struct hq9x_object hq9x_object_t;
typedef void (*hq9x_method_t)(hq9x_object_t *, hq9x_state_t *);

struct hq9x_class
{
	hq9x_class_t * isa;
	hq9x_class_t * superclass;
	hq9x_method_t destroy;
	hq9x_method_t exception;
};

struct hq9x_object
{
	hq9x_class_t * isa;
};

typedef struct hq9x_oo_state
{
	hq9x_class_t meta_class;
	hq9x_class_t generic_class;
	hq9x_class_t * current_class;
	hq9x_object_t * current_object;
} hq9x_oo_state_t;

/* The HQ9+ interpreter state */

struct hq9x_state
{
	function_ptr_t ops[256];
	function_ptr_t pre_op, default_op, op, last_op;
	char charcase; /* 0, 'A' or 'a' */
	source_t source;
	source_t input;
	char opchar;

	int accumulator;
	enum { ERROR_QUIET, ERROR_SIGNAL, ERROR_HALT } on_error;
	int exit_with_accumulator; /* on exit, use accumulator as program status */

	bf_state_t * bf;
	bef_state_t * bef;
	hq9x_oo_state_t * oo;
};

enum
{
	HQ9X_ORIGINAL,
	HQ9X_DEFAULT,
	HQ9X_CHIQRSX9X,
	HQ9X_BRAINF,
	HQ9X_OO,
	HQ9X_OO_QC,
	HQ9X_H9X,
	HQ9X_HQ9XBF,
	HQ9X_DEADFISH,
	HQ9X_FISHQ9X,
	HQ9X_2D,
	HQ9X_BEFUNGE93,
	HQ9X_NIL,
	HQ9X_PLUS,
	HQ9X_H9F,
};

/* BF interpreter */

typedef char bf_cell_t;

static void bf_init(hq9x_state_t * state)
{
	if(!state->bf)
		state->bf = clear_alloc(sizeof(bf_state_t));
	if(!state->bf->cells)
		state->bf->cells = malloc(sizeof(bf_cell_t) * (state->bf->count = 100));
	memset(state->bf->cells, 0, sizeof(bf_cell_t) * state->bf->count);
	state->bf->pointer = 0;
}

/*static void bf_debug(hq9x_state_t * state)
{
	fprintf(stderr, "Codepoint %ld, BF pointer at %ld with value %d\n", state->source.pointer - state->source.text, state->bf->pointer, state->bf->cells[state->bf->pointer]);
}*/

/*static void bf_clear(hq9x_state_t * state)
{
	if(state->bf)
	{
		if(state->bf->cells)
			free(state->bf->cells);
		free(state->bf);
		state->bf = NULL;
	}
}*/

void bf_left(hq9x_state_t * state)
{
	if(state->bf->pointer > 0)
		state->bf->pointer --;
}

void bf_right(hq9x_state_t * state)
{
	if(state->bf->pointer < 30000)
		state->bf->pointer ++;
	if(state->bf->pointer > state->bf->count)
	{
		state->bf->cells = realloc(state->bf->cells, sizeof(bf_cell_t) * (state->bf->count + 100));
		memset(state->bf->cells + sizeof(bf_cell_t) * state->bf->count, 0, sizeof(bf_cell_t) * 100);
		state->bf->count += 100;
	}
}

void bf_inc(hq9x_state_t * state)
{
	++state->bf->cells[state->bf->pointer];
}

void bf_dec(hq9x_state_t * state)
{
	--state->bf->cells[state->bf->pointer];
}

void bf_do(hq9x_state_t * state)
{
	if(!state->bf->cells[state->bf->pointer])
	{
		int level = 0;
		state->source.pointer ++;
		while(1)
		{
			if(*state->source.pointer == '\0')
				break;
			if(*state->source.pointer == '[')
				level ++;
			else if(*state->source.pointer == ']' && level-- == 0)
				break;
			state->source.pointer ++;
		}
	}
}

void bf_loop(hq9x_state_t * state)
{
	if(state->bf->cells[state->bf->pointer])
	{
		int level = 0;
		while(1)
		{
			if(state->source.pointer == state->source.text)
			{
				state->source.pointer = 0;
				break;
			}
			state->source.pointer --;
			if(*state->source.pointer == ']')
				level ++;
			else if(*state->source.pointer == '[' && level-- == 0)
				break;
		}
	}
}

void bf_read(hq9x_state_t * state)
{
	if((((bf_cell_t *)state->bf->cells)[state->bf->pointer] = *source_get_pointer(&state->input)))
		state->input.pointer ++;
}

void bf_write(hq9x_state_t * state)
{
	putchar(((bf_cell_t *)state->bf->cells)[state->bf->pointer]);
}

/* Befunge, HQ9+2D commands */

void bef_up(hq9x_state_t * state)
{
	state->source.dir = '^';
}

void bef_down(hq9x_state_t * state)
{
	state->source.dir = 'v';
}

void bef_left(hq9x_state_t * state)
{
	state->source.dir = '<';
}

void bef_right(hq9x_state_t * state)
{
	source_get_lines(&state->source);
	state->source.dir = '>';
}

/* Befunge interpreter */

static void bef_init(hq9x_state_t * state)
{
	if(!state->bef)
		state->bef = clear_alloc(sizeof(bef_state_t));
	if(!state->bef->stack)
		state->bef->stack = malloc(sizeof(bef_cell_t) * (state->bef->capacity = 16));
	memset(state->bef->stack, 0, sizeof(bef_cell_t) * state->bef->capacity);
	state->bef->pointer = 0;
}

/*static void bef_debug(hq9x_state_t * state)
{
	char ** line;
	fprintf(stderr, "BEF pointer at (%ld, %ld), stack with value %ld\n", state->source.pointer - *state->source.line, state->source.line - state->source.lines, state->bef->pointer ? state->bef->stack[state->bef->pointer - 1] : 0);
	for(line = state->source.lines; *line; line++)
		printf("[%s]\n", *line);
}*/

/*static void bef_clear(hq9x_state_t * state)
{
	if(state->bef)
	{
		if(state->bef->stack)
			free(state->bef->stack);
		free(state->bef);
		state->bef = NULL;
	}
}*/

static bef_cell_t bef_peek(hq9x_state_t * state)
{
	if(state->bef->pointer <= 0)
	{
		state->bef->pointer = 0;
		return 0;
	}
	return state->bef->stack[state->bef->pointer - 1];
}

static bef_cell_t bef_pop(hq9x_state_t * state)
{
	if(state->bef->pointer > 0)
		return state->bef->stack[--state->bef->pointer];
	state->bef->pointer = 0;
	return 0;
}

static void bef_push(hq9x_state_t * state, bef_cell_t value)
{
	if(state->bef->pointer <= 0)
		state->bef->pointer = 0;
	while(state->bef->capacity < state->bef->pointer + 1)
		state->bef->stack = realloc(state->bef->stack, sizeof(bef_cell_t) * (state->bef->capacity += 16));
	state->bef->stack[state->bef->pointer++] = value;
}

void bef_add(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	bef_push(state, a + b);
}

void bef_sub(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	bef_push(state, a - b);
}

void bef_mul(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	bef_push(state, a * b);
}

void bef_div(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	if(b < 0)
	{
		bef_push(state, (a + b - 1) / b);
	}
	else if(b == 0)
	{
		printf("Division by zero; please specify result: ");
		fflush(stdout);
		scanf("%ld", &a);
		bef_push(state, a);
	}
	else
	{
		bef_push(state, a / b);
	}
}

void bef_mod(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	if(b < 0)
	{
		bef_push(state, (a % b - b) % -b);
	}
	else if(b == 0)
	{
		printf("Modulo by zero; please specify result: ");
		fflush(stdout);
		scanf("%ld", &a);
		bef_push(state, a);
	}
	else
	{
		bef_push(state, a % b);
	}
}

void bef_not(hq9x_state_t * state)
{
	bef_push(state, bef_pop(state) == 0 ? 1 : 0);
}

void bef_greater(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	bef_push(state, a > b ? 1 : 0);
}

void bef_random(hq9x_state_t * state)
{
	char dirs[] = ">^<v";
	state->source.dir = dirs[rand() & 3];
}

void bef_h_if(hq9x_state_t * state)
{
	state->source.dir = bef_pop(state) == 0 ? '>' : '<';
}

void bef_v_if(hq9x_state_t * state)
{
	state->source.dir = bef_pop(state) == 0 ? 'v' : '^';
}

void bef_string(hq9x_state_t * state)
{
	state->bef->stringmode = !state->bef->stringmode;
}

void bef_dup(hq9x_state_t * state)
{
	bef_push(state, bef_peek(state));
}

void bef_swap(hq9x_state_t * state)
{
	bef_cell_t a, b;
	b = bef_pop(state);
	a = bef_pop(state);
	bef_push(state, b);
	bef_push(state, a);
}

void bef_drop(hq9x_state_t * state)
{
	bef_pop(state);
}

void bef_print_int(hq9x_state_t * state)
{
	printf("%ld\n", bef_pop(state));
}

void bef_print_char(hq9x_state_t * state)
{
	printf("%c", (char)bef_pop(state));
}

void bef_scan_int(hq9x_state_t * state)
{
	bef_cell_t v = 0, sgn = 1;
	int c;
	if(*source_get_pointer(&state->input) == '-')
	{
		sgn = -1;
		source_advance(&state->input);
	}
	else if(*source_get_pointer(&state->input) == '+')
	{
		source_advance(&state->input);
	}
	while(isdigit((c = *source_get_pointer(&state->input))))
	{
		v = 10 * v + c;
		source_advance(&state->input);
	}
	bef_push(state, sgn * v);
}

void bef_scan_char(hq9x_state_t * state)
{
	bef_push(state, *source_get_pointer(&state->input));
	source_advance(&state->input);
}

void bef_bridge(hq9x_state_t * state)
{
	source_advance(&state->source);
}

void bef_get(hq9x_state_t * state)
{
	bef_cell_t x, y;
	y = bef_pop(state);
	x = bef_pop(state);
	if(0 <= y && y < state->source.count && 0 <= x && x < strlen(state->source.lines[y]))
		bef_push(state, (unsigned char)state->source.lines[y][x]);
	else
		bef_push(state, 0);
}

void bef_put(hq9x_state_t * state)
{
	bef_cell_t x, y, v;
	y = bef_pop(state);
	x = bef_pop(state);
	v = bef_pop(state);
	if(0 <= y && y < state->source.count && 0 <= x && x < strlen(state->source.lines[y]))
		state->source.lines[y][x] = v;
}

void bef_push_digit(hq9x_state_t * state)
{
	bef_push(state, state->opchar - '0');
}

void hq9x_nop(hq9x_state_t * state);
void bef_preprocess(hq9x_state_t * state)
{
	if(state->bef->stringmode && state->opchar != '"')
	{
		state->op = hq9x_nop;
		bef_push(state, state->opchar);
	}
}

/* The core HQ9+ runtime */

static const char * hq9x_hello_message = "Hello, world!";

/* halt on error */

static void hq9x_error_halt(hq9x_state_t * state)
{
	if(state->on_error == ERROR_HALT)
		exit(1);
}

/* comment (ignored character) */

void hq9x_nop(hq9x_state_t * state)
{
}

/* syntax error */

void hq9x_unknown(hq9x_state_t * state)
{
	if(state->on_error != ERROR_QUIET)
	{
		fprintf(stderr, "Unknown command: `%c'\n", state->opchar);
		hq9x_error_halt(state);
	}
}

/* add newline, FISHQ9+ unknown command */

void hq9x_newline(hq9x_state_t * state)
{
	putchar('\n');
}

/* HQ9+ command H */

void hq9x_hello(hq9x_state_t * state)
{
	printf("%s\n", hq9x_hello_message);
}

/* HQ9+ command Q */

void hq9x_quine(hq9x_state_t * state)
{
	printf("%s", state->source.text);
}

/* HQ9+ command 9 */

void hq9x_bottles(hq9x_state_t * state)
{
	int i;
	for(i = 99; i > 0; i--)
	{
		printf("%d bottle%s of beer on the wall,\n", i, i == 1 ? "" : "s");
		printf("%d bottle%s of beer.\n", i, i == 1 ? "" : "s");
		printf("Take one down, pass it around,\n");
		if(i != 1)
			printf("%d bottle%s of beer on the wall.\n", i - 1, i == 2 ? "" : "s");
		else
			printf("No bottles of beer on the wall.\n");
	}
}

/* HQ9+ command +; FISHQ9+, Deadfish command I (also X) */

void hq9x_inc(hq9x_state_t * state)
{
	state->accumulator ++;
}

/* HQ9+- command --; FISHQ9+ command D/d */

void hq9x_dec(hq9x_state_t * state)
{
	state->accumulator --;
}

/* FISHQ9+, Deadfish command S/s (also k) */

void hq9x_square(hq9x_state_t * state)
{
	state->accumulator = state->accumulator * state->accumulator;
}

/* FISHQ9+, Deadfish command O/o (also c) */

void hq9x_output(hq9x_state_t * state)
{
	printf("%d\n", state->accumulator);
}

/* FISHQ9+ command K/k; Deadfish command h */

void hq9x_kill(hq9x_state_t * state)
{
	exit(state->exit_with_accumulator ? state->accumulator : 0);
}

/* FISHQ9+, Deadfish operation before each statement */

void hq9x_force_bound(hq9x_state_t * state)
{
	if(state->accumulator == -1 || state->accumulator == 256)
		state->accumulator = 0;
}

/* CHIKRSX9+ command I */

void hq9x_interpret(hq9x_state_t * state)
{
	source_t old;
	char * old_input;
	function_ptr_t old_op = state->last_op;

	old = state->source;

	/* store old source data, move input to source */
	source_get_text(&state->input);
	state->source = state->input;
	old_input = state->input.pointer;
	state->source.pointer = NULL;

	source_init(&state->input, stdin);
	state->last_op = NULL;

	source_get_pointer(&state->source); /* ensure input is ready */
	while(!state->source.out_of_bound)
	{
		unsigned char op = *source_get_pointer(&state->source);
		switch(state->charcase)
		{
		case 0:
			if('A' <= op && op <= 'Z')
				op += 'a' - 'A';
		break;
		case 'A':
			if('A' <= op && op <= 'Z')
				op += 'a' - 'A';
			else if('a' <= op && op <= 'z')
				op = 0;
		break;
		case 'a':
		break;
		}
		state->opchar = op;
		state->op = state->ops[op];

		/* do any pre-operation (probably null) */
		state->pre_op(state);
		if(state->op)
		{
			state->op(state);
			state->last_op = state->op;
		}
		else
		{
			state->op = state->default_op;
			state->default_op(state);
			//if(state->op)
				state->last_op = state->op;
		}
		source_advance(&state->source);
	}

	source_free(&state->input);
	state->input = state->source;
	state->input.pointer = old_input;
	state->source = old;

	state->last_op = old_op;
}

/* HQ9+B command B */

void hq9x_interpret_bf(hq9x_state_t * state)
{
	function_ptr_t old_ops[256];
	function_ptr_t old_pre_op, old_default_op;

	memcpy(old_ops, state->ops, sizeof(old_ops));
	memset(state->ops, 0, sizeof(old_ops));
	state->ops['<'] = bf_left;
	state->ops['>'] = bf_right;
	state->ops['+'] = bf_inc;
	state->ops['-'] = bf_dec;
	state->ops['['] = bf_do;
	state->ops[']'] = bf_loop;
	state->ops['.'] = bf_write;
	state->ops[','] = bf_read;
	bf_init(state);
	state->bf->enabled = 1;

	old_pre_op = state->pre_op;
	state->pre_op = hq9x_nop;

	old_default_op = state->default_op;
	state->default_op = hq9x_nop;

	hq9x_interpret(state);


	memcpy(state->ops, old_ops, sizeof(old_ops));
	memset(state->ops, 0, sizeof(old_ops));

	state->pre_op = old_pre_op;

	state->default_op = old_default_op;
}

/* CHIKRSX9+ command C */

void hq9x_copy(hq9x_state_t * state)
{
	printf("%s", source_get_text(&state->input));
}

/* CHIKRSX9+ command R */

void hq9x_rot13(hq9x_state_t * state)
{
	char * pointer = source_get_text(&state->input);
	while(*pointer)
	{
		int c = *pointer++;
		if(('A' <= c && c <= 'M') || ('a' <= c && c <= 'm'))
			c += 13;
		else if(('N' <= c && c <= 'Z') || ('n' <= c && c <= 'z'))
			c -= 13;
		putchar(c);
	}
}

static int strpcmp(const void * first, const void * second)
{
	return strcmp(*(const char **)first, *(const char **)second);
}

/* CHIKRSX9+ command S */

void hq9x_sort(hq9x_state_t * state)
{
	char ** arr = source_get_lines(&state->input);
	size_t counter;
	qsort(arr, state->input.count, sizeof(char *), strpcmp);
	for(counter = 0; arr[counter]; counter++)
	{
		printf("%s\n", arr[counter]);
	}
}

/* CHIKRSX9+ command X - implemented differently from reference implementation, parsing BF commands until next X */

void hq9x_turing(hq9x_state_t * state)
{
	state->bf->enabled = !state->bf->enabled;
}

/* CHIKRSX9+ command X subcommands */

static void hq9x_pre_alter_bf(hq9x_state_t * state)
{
	if(state->bf && state->bf->enabled)
	{
		switch(state->opchar)
		{
		case '<':
			state->op = bf_left;
		break;
		case '>':
			state->op = bf_right;
		break;
		case '+':
			state->op = bf_inc;
		break;
		case '-':
			state->op = bf_dec;
		break;
		case '[':
			state->op = bf_do;
		break;
		case ']':
			state->op = bf_loop;
		break;
		case ',':
			state->op = bf_read;
		break;
		case '.':
			state->op = bf_write;
		break;
		}
	}
}

/* Object orientation */

static void hq9x_destroy_object(hq9x_object_t * object, hq9x_state_t * state)
{
	if(object != (hq9x_object_t *)&state->oo->meta_class && object != (hq9x_object_t *)&state->oo->generic_class)
		free(object);
}

static void hq9x_raise_exception(hq9x_object_t * object, hq9x_state_t * state)
{
	fprintf(stderr, "Unhandled virtual exception\n");
	exit(1);
}

static void hq9x_oo_init(hq9x_state_t * state)
{
	if(!state->oo)
	{
		state->oo = clear_alloc(sizeof(hq9x_oo_state_t));
		state->oo->meta_class.isa = &state->oo->meta_class;
		state->oo->meta_class.superclass = &state->oo->generic_class;
		state->oo->meta_class.destroy = hq9x_destroy_object;
		state->oo->meta_class.exception = hq9x_raise_exception;
		state->oo->generic_class.isa = &state->oo->meta_class;
		state->oo->generic_class.superclass = &state->oo->generic_class;
		state->oo->generic_class.destroy = hq9x_destroy_object;
		state->oo->generic_class.exception = hq9x_raise_exception;
	}
	if(state->oo->current_object)
	{
		state->oo->current_object->isa->destroy(state->oo->current_object, state);
		state->oo->current_object = NULL;
	}
	if(state->oo->current_class)
	{
		state->oo->current_class->isa->destroy((hq9x_object_t *)state->oo->current_class, state);
		state->oo->current_class = NULL;
	}
}

static hq9x_class_t * hq9x_new_class(hq9x_state_t * state)
{
	hq9x_class_t * new_class = clear_alloc(sizeof(hq9x_class_t));
	new_class->isa = &state->oo->meta_class;
	new_class->superclass = &state->oo->generic_class;
	new_class->destroy = hq9x_destroy_object;
	new_class->exception = hq9x_raise_exception;
	return new_class;
}

static hq9x_object_t * hq9x_new_object(hq9x_object_t * class_object, hq9x_state_t * state)
{
	hq9x_object_t * new_object = clear_alloc(sizeof(hq9x_object_t));
	new_object->isa = (hq9x_class_t *)class_object;
	return new_object;
}

static void hq9x_new(hq9x_state_t * state)
{
	hq9x_oo_init(state);
	state->oo->current_class = hq9x_new_class(state);
	state->oo->current_object = hq9x_new_object((hq9x_object_t *)state->oo->current_class, state);
}

/* HQ9++ command + and ++ */

void hq9x_inc_or_alloc(hq9x_state_t * state)
{
	if(state->last_op != hq9x_inc)
		state->op = hq9x_inc, hq9x_inc(state);
	else
		state->op = hq9x_new, hq9x_new(state);
}

static void hq9x_io_error(hq9x_state_t * state)
{
	/* TODO */
}

static void hq9x_divide_by_zero(hq9x_state_t * state)
{
	state->accumulator -= state->accumulator;
	state->accumulator = 1 / state->accumulator;
}

static void hq9x_out_of_stack(hq9x_state_t * state)
{
	hq9x_out_of_stack(state);
	hq9x_out_of_stack(state); /* to assure no optimization is done */
}

static void hq9x_infinite_loop(hq9x_state_t * state)
{
	while(1);
}

/* HQ9+- command - */

void hq9x_quality_control(hq9x_state_t * state)
{
	if(state->bf && state->bf->enabled)
	{
		/* if BF commands are switched on, override "quality control" */
		state->last_op = bf_dec;
		bf_dec(state);
		return;
	}

	if(state->last_op == NULL)
	{
		unsigned char next = *state->source.pointer;
		if(state->ops[next] == hq9x_quality_control)
		{
			/* This is probably not how the author intended it, but this makes the most "sense" */
			state->source.pointer ++;
			state->op = hq9x_dec;
			hq9x_dec(state);
		}
		else
		{
			if(state->on_error != ERROR_QUIET)
			{
				fprintf(stderr, "Syntax error\n");
				hq9x_error_halt(state);
			}
			exit(1);
		}
	}
	else if(state->last_op == hq9x_hello)
	{
		hq9x_io_error(state);
		/* TODO */
		fprintf(stderr, "I/O error\n");
		exit(1);
	}
	else if(state->last_op == hq9x_quine)
	{
		hq9x_out_of_stack(state);
		/* unreachable */
		fprintf(stderr, "Out of stack\n");
		exit(1);
	}
	else if(state->last_op == hq9x_bottles)
	{
		hq9x_infinite_loop(state);
		/* unreachable */
		fprintf(stderr, "Infinite loop\n");
		exit(1);
	}
	else if(state->last_op == hq9x_inc)
	{
		hq9x_divide_by_zero(state);
		/* unreachable */
		fprintf(stderr, "Division by zero\n");
		exit(1);
	}
	else if(state->last_op == hq9x_new)
	{
		state->oo->current_object->isa->exception(state->oo->current_object, state);
		/* unreachable */
		fprintf(stderr, "Unhandled virtual exception\n");
		exit(1);
	}
	else
	{
		fprintf(stderr, "Unknown error, please contact the author of the software\n");
		exit(1);
	}
}

/* Deep Though API */

static void * dt_init(void)
{
	return malloc(sizeof(long));
}

static void dt_process(void * state)
{
	*(long *)state = 42;
}

static void dt_log(void * state, FILE * file)
{
	fprintf(file, "%ld\n", *(long *)state);
}

static void dt_free(void * state)
{
	free(state);
}

/* H9F DT operator */

/* TODO: the interpreter should probably consider two-character operators, as this cannot be combined with any other preprocessed dialect */

static void hq9x_dt_d(hq9x_state_t * state)
{
}

void hq9x_dt_invoke(hq9x_state_t * state)
{
	void * dt_state = dt_init();
	dt_process(dt_state);
	dt_log(dt_state, stdout);
	dt_free(dt_state);
}

/* H9F preprocessor operator */

static void hq9x_check_dt(hq9x_state_t * state)
{
	if(state->last_op == hq9x_dt_d)
	{
		if(state->opchar == 't')
		{
			state->op = hq9x_dt_invoke;
		}
		else
		{
			int tmp = state->opchar;
			state->opchar = 'd';
			hq9x_unknown(state);
			state->opchar = tmp;
		}
	}
}

/* initialization */

static void hq9x_initialize_bf(hq9x_state_t * state)
{
	state->ops['<'] = bf_left;
	state->ops['>'] = bf_right;
	state->ops['+'] = bf_inc;
	state->ops['-'] = bf_dec;
	state->ops['['] = bf_do;
	state->ops[']'] = bf_loop;
	state->ops['.'] = bf_write;
	state->ops[','] = bf_read;
	bf_init(state);
	state->bf->enabled = 1;
}

void hq9x_initialize(hq9x_state_t * state, int version)
{
	state->ops['\0'] = hq9x_nop; /* end of file */
	switch(version)
	{
	case HQ9X_DEFAULT:
		state->ops['k'] = hq9x_kill;
	case HQ9X_CHIQRSX9X:
		state->ops['c'] = hq9x_copy;
		state->ops['i'] = hq9x_interpret;
		state->ops['r'] = hq9x_rot13;
		state->ops['s'] = hq9x_sort;
		state->ops['x'] = hq9x_turing;

	/* Turing-completeness is enabled through BF-parsing */
		bf_init(state);
		state->bf->enabled = 0;
	/* fallthru; */
	case HQ9X_ORIGINAL:
	case HQ9X_OO:
	case HQ9X_HQ9XBF:
	case HQ9X_H9F:
	default:
		state->ops['q'] = hq9x_quine;
	case HQ9X_H9X:
		state->ops['h'] = hq9x_hello;
		state->ops['9'] = hq9x_bottles;
	case HQ9X_PLUS:
		state->ops['+'] = hq9x_inc;
	case HQ9X_NIL:
	break;

	case HQ9X_FISHQ9X:
		state->ops['h'] = hq9x_hello;
		state->ops['q'] = hq9x_quine;
		state->ops['9'] = hq9x_bottles;
		state->ops['+'] = hq9x_inc;
	case HQ9X_DEADFISH:
		state->ops['i'] = hq9x_inc;
		state->ops['d'] = hq9x_dec;
		state->ops['s'] = hq9x_square;
		state->ops['o'] = hq9x_output;
	break;

	case HQ9X_BRAINF:
		hq9x_initialize_bf(state);
	break;

	case HQ9X_BEFUNGE93:
		state->ops['+'] = bef_add;
		state->ops['-'] = bef_sub;
		state->ops['*'] = bef_mul;
		state->ops['/'] = bef_div;
		state->ops['%'] = bef_mod;
		state->ops['!'] = bef_not;
		state->ops['`'] = bef_greater;
		state->ops['>'] = bef_right;
		state->ops['<'] = bef_left;
		state->ops['^'] = bef_up;
		state->ops['v'] = bef_down;
		state->ops['?'] = bef_random;
		state->ops['_'] = bef_h_if;
		state->ops['|'] = bef_v_if;
		state->ops['"'] = bef_string;
		state->ops[':'] = bef_dup;
		state->ops['\\'] = bef_swap;
		state->ops['$'] = bef_drop;
		state->ops['.'] = bef_print_int;
		state->ops[','] = bef_print_char;
		state->ops['#'] = bef_bridge;
		state->ops['g'] = bef_get;
		state->ops['p'] = bef_put;
		state->ops['&'] = bef_scan_int;
		state->ops['~'] = bef_scan_char;
		state->ops['@'] = hq9x_kill;
		state->ops['0'] = bef_push_digit;
		state->ops['1'] = bef_push_digit;
		state->ops['2'] = bef_push_digit;
		state->ops['3'] = bef_push_digit;
		state->ops['4'] = bef_push_digit;
		state->ops['5'] = bef_push_digit;
		state->ops['6'] = bef_push_digit;
		state->ops['7'] = bef_push_digit;
		state->ops['8'] = bef_push_digit;
		state->ops['9'] = bef_push_digit;
		state->ops[' '] = hq9x_nop;
		bef_init(state);
		state->bef->enabled = 1;
	break;
	}

	source_init(&state->source, stdin);
	source_init(&state->input, stdin);
	state->source.text = "";
	state->accumulator = 0;

	if(version == HQ9X_H9F)
	{
		hq9x_initialize_bf(state);
		state->ops['d'] = hq9x_dt_d;
	}
	if(version == HQ9X_DEFAULT || version == HQ9X_OO || version == HQ9X_OO_QC)
		state->ops['+'] = hq9x_inc_or_alloc;
	if(version == HQ9X_DEFAULT || version == HQ9X_OO_QC)
		state->ops['-'] = hq9x_quality_control;
	if(version == HQ9X_DEFAULT || version == HQ9X_HQ9XBF)
		state->ops['b'] = hq9x_interpret_bf;
	if(version == HQ9X_DEFAULT || version == HQ9X_2D)
	{
		state->ops['<'] = bef_left;
		state->ops['>'] = bef_right;
		state->ops['v'] = bef_down;
		state->ops['^'] = bef_up;
	}

	if(version == HQ9X_DEADFISH || version == HQ9X_FISHQ9X)
		state->pre_op = hq9x_force_bound;
	else if(version == HQ9X_DEFAULT || version == HQ9X_CHIQRSX9X)
		state->pre_op = hq9x_pre_alter_bf;
	else if(version == HQ9X_BEFUNGE93)
		state->pre_op = bef_preprocess;
	else if(version == HQ9X_H9F)
		state->pre_op = hq9x_check_dt;
	else
		state->pre_op = hq9x_nop;

	switch(version)
	{
	case HQ9X_ORIGINAL:
	case HQ9X_CHIQRSX9X:
	case HQ9X_OO:
	case HQ9X_HQ9XBF:
	case HQ9X_H9F:
	case HQ9X_BEFUNGE93: /* it is treated so in the reference implementation */
		state->default_op = hq9x_unknown;
		state->on_error = ERROR_HALT;
	break;

	case HQ9X_DEFAULT:
	case HQ9X_BRAINF:
	case HQ9X_OO_QC:
	case HQ9X_FISHQ9X:
	case HQ9X_H9X:
	case HQ9X_PLUS:
	case HQ9X_NIL:
	default:
		//state->default_op = hq9x_nop;
		state->default_op = hq9x_unknown;
		state->on_error = ERROR_QUIET;
	break;

	case HQ9X_DEADFISH:
		state->default_op = hq9x_newline;
	break;
	}

	switch(version)
	{
	case HQ9X_ORIGINAL:
	//case HQ9X_OO:
	case HQ9X_DEADFISH:
	case HQ9X_BEFUNGE93: /* it is treated so in the reference implementation */
		state->charcase = 'a';
	break;

	case HQ9X_OO_QC:
	case HQ9X_CHIQRSX9X:
	case HQ9X_DEFAULT:
	case HQ9X_FISHQ9X:
	//case HQ9X_BRAINF:
	//case HQ9X_HQ9:
	//case HQ9X_HQ9XBF:
	//case HQ9X_PLUS:
	//case HQ9X_NIL:
	default:
		state->charcase = 0;
	break;

	case HQ9X_H9F: /* this is what the default implementation does */
		state->charcase = 'A';
	break;
	}
}

void show_version(void)
{
	printf(HQ9X_VERSION);
	exit(0);
}

void usage(char * argv0)
{
	printf("Usage: %s <options>? <inputfile>?\n\
Valid options:\n\
\t-a\tOn exit, give accumulator as result\n\
\t-c<chr>\tCase sensitivity, values:\n\
\t\t0 - insensitive ('X' and 'x' are the same)\n\
\t\ta - lower case ('X' and 'x' are different)\n\
\t\tA - upper case ('x' is invalid)\n\
\t\td - default for dialect\n\
\t-h\tThis help page\n\
\t-n<chr>\tOperation on newline:\n\
\t\ti - ignore\n\
\t\tu - unknown (signal if enabled)\n\
\t\tw - as whitespace (default)\n\
\t-m\tMessage to write on command H\n\
\t-u<chr>\tOperation on unknown command:\n\
\t\th - signal error and halt\n\
\t\tn - insert newline (Deadfish)\n\
\t\tq - quiet (comment)\n\
\t\ts - signal error\n\
\t-v\tCurrent version\n\
\t-x\tChange dialect, currently supported:\n\
\t\tall\t\t\tThe default dialect, a combination of several extensions\n\
\t\tb hq9+ hq9x\t\tOriginal HQ9+ by Cliff L. Biffle\n\
\t\t\thttp://web.archive.org/web/20090602074545/http://www.cliff.biffle.org/esoterica/hq9plus.html\n\
\t\to chiqrsx9+ chiqrsx9x\tCHIQRSX9+, A Turing-complete extension by Ørjen Johansen\n\
\t\t\thttp://oerjan.nvg.org/esoteric/chiqrsx9+.pl\n\
\t\tm hq9++ hq9xx\t\tObject-oriented extension by David Morgan-Mar\n\
\t\t\thttp://www.dangermouse.net/esoteric/hq9plusplus.html\n\
\t\tz hq9+-\t\t\tObject-oriented extension with quality control by melikamp\n\
\t\t\thttp://melikamp.com/features/hq9pm.shtml\n\
\t\th9+ h9x\t\t\tA restriction, presumably by User:Phantom Hoover\n\
\t\t\thttp://esolangs.org/wiki/H9%%2B\n\
\t\thq9+b hq9xb\t\tAn extension that is ℒ-complete, by User:Oleg\n\
\t\t\thttp://esolangs.org/wiki/HQ9%%2BB\n\
\t\t2d hq9+2d hq9x2d\tAn extension where the program counter moves in 4 directions, by Kenner Gordon\n\
\t\t\thttp://esolangs.org/wiki/HQ9%%2B2D\n\
\t\tbf\t\t\tThe BrainF language by Urban Müller\n\
\t\tdf deadfish\t\tThe Deadfish language, a HQ9+ derivative by Jonathan Todd Skinner\n\
\t\t\thttp://web.archive.org/web/20100425075447/http://www.jonathantoddskinner.com/projects/deadfish.html\n\
\t\tfishq9+ fishq9x\t\tA combination of Deadfish and HQ9+ by User:Erinius\n\
\t\t\thttp://esolangs.org/wiki/FISHQ9%%2B\n\
\t\tbf93\t\t\tThe Befunge-93 language by Chris Pressey\n\
\t\t\thttp://catseye.tc/node/Befunge-93.html\n\
\t\th9f\t\t\tThe Hq9eF language by Egor Promyshlennikov (2016)\n\
\t\t\thttp://esolangs.org/wiki/Special:Search/H9F\n\
\t\t+\t\t\tThe + language, possibly by User:HolyCloudNinja\n\
\t\t\thttp://esolangs.org/wiki/%%2B\n\
\t\tnil\t\t\tThe Nil language by Sascha René Leib\n\
\t\t\thttp://esolangs.org/wiki/%%2B\n\
\t-w<chr>\tOperation on whitespace:\n\
\t\ti - ignore (default)\n\
\t\tu - unknown (signal if enabled)\n\
",
	argv0);
	/* melikamp is referred to as Ivan Grigoryevich Zaigralin in the sources */
	exit(0);
}

int main(int argc, char ** argv)
{
	int argp = 1;
	FILE * source = stdin;
	hq9x_state_t * state = clear_alloc(sizeof(hq9x_state_t));
	int version = HQ9X_DEFAULT;
	int charcase = -1;
	int defop = 0;
	int on_nl = 'w', on_ws = 'i'; /* on newline, on whitespace */

	while(argp < argc)
	{
		if(argv[argp][0] == '-')
		{
			switch(argv[argp][1])
			{
			case 'a':
				state->exit_with_accumulator = 1;
			break;
			case 'c':
				switch(argv[argp][2])
				{
				case '0':
					charcase = 0;
				break;
				case 'a':
					charcase = 'a';
				break;
				case 'A':
					charcase = 'A';
				break;
				case 'd':
					charcase = -1;
				break;
				default:
					fprintf(stderr, "Unknown case-sensitivity: %c\n", argv[argp][2]);
				}
			break;
			case 'u':
				switch(argv[argp][2])
				{
				case 'n':
				case 'q': /* HQ9+ C interpreter used -q */
				case 's':
				case 'h': /* HQ9+ C interpreter used -9 */
					defop = argv[argp][2];
				break;
				default:
					fprintf(stderr, "Unknown default operation: %c\n", argv[argp][2]);
				}
			break;
			case 'w':
				switch(argv[argp][2])
				{
				case 'u':
				case 'i':
					on_ws = argv[argp][2];
				break;
				default:
					fprintf(stderr, "Unknown whitespace operation: %c\n", argv[argp][2]);
				}
			break;
			case 'n':
				switch(argv[argp][2])
				{
				case 'w':
				case 'u':
				case 'i':
					on_nl = argv[argp][2];
				break;
				default:
					fprintf(stderr, "Unknown newline operation: %c\n", argv[argp][2]);
				}
			break;
			default:
				fprintf(stderr, "Invalid option: -%c\n", argv[argp][1]);
			case 'h':
				usage(argv[0]);
			break;
			case 'm': /* HQ9+ C interpreter used -H to switch between two messages */
				argp++;
				if(argv[argp])
					hq9x_hello_message = argv[argp];
				else
				{
					fprintf(stderr, "Expected: message\n");
					return 1;
				}
			break;
			case 'v':
				show_version();
			break;
			case 'x':
				argp++;
				if(argp >= argc)
				{
					printf("Missing dialect\n");
					return 1;
				}
				if(strcasecmp(argv[argp], "HQ9+") == 0 || strcasecmp(argv[argp], "HQ9X") == 0 || strcmp(argv[argp], "b") == 0)
					version = HQ9X_ORIGINAL;
				else if(strcasecmp(argv[argp], "CHIQRSX9+") == 0 || strcasecmp(argv[argp], "CHIQRSX9X") == 0 || strcmp(argv[argp], "o") == 0)
					version = HQ9X_CHIQRSX9X;
				else if(strcasecmp(argv[argp], "ALL") == 0)
					version = HQ9X_DEFAULT;
				else if(strcasecmp(argv[argp], "BF") == 0)
					version = HQ9X_BRAINF;
				else if(strcasecmp(argv[argp], "HQ9++") == 0 || strcasecmp(argv[argp], "HQ9XX") == 0 || strcmp(argv[argp], "m") == 0)
					version = HQ9X_OO;
				else if(strcasecmp(argv[argp], "HQ9+-") == 0 || strcmp(argv[argp], "z") == 0)
					version = HQ9X_OO_QC;
				else if(strcasecmp(argv[argp], "DF") == 0 || strcasecmp(argv[argp], "DEADFUSH") == 0)
					version = HQ9X_DEADFISH;
				else if(strcasecmp(argv[argp], "FISHQ9+") == 0 || strcasecmp(argv[argp], "FISHQ9X") == 0)
					version = HQ9X_FISHQ9X;
				else if(strcasecmp(argv[argp], "H9+") == 0 || strcasecmp(argv[argp], "H9X") == 0)
					version = HQ9X_H9X;
				else if(strcasecmp(argv[argp], "HQ9+B") == 0 || strcasecmp(argv[argp], "HQ9XB") == 0)
					version = HQ9X_HQ9XBF;
				else if(strcasecmp(argv[argp], "BF93") == 0)
					version = HQ9X_BEFUNGE93;
				else if(strcasecmp(argv[argp], "H9F") == 0)
					version = HQ9X_H9F;
				else if(strcasecmp(argv[argp], "NIL") == 0)
					version = HQ9X_NIL;
				else if(strcmp(argv[argp], "+") == 0)
					version = HQ9X_PLUS;
				else
				{
					printf("Unrecognized dialect: %s\n", argv[argp]);
					return 1;
				}
			}
		}
		else
		{
			source = fopen(argv[argp], "r");
			if(!source)
			{
				fprintf(stderr, "Unable to open %s for reading\n", argv[1]);
				return 1;
			}
			break;
		}
		argp++;
	}
	hq9x_initialize(state, version);
	if(charcase != -1)
		state->charcase = charcase;
	switch(defop)
	{
	case 'n':
		state->default_op = hq9x_newline;
	break;
	case 'q':
		state->default_op = hq9x_unknown;
		state->on_error = ERROR_QUIET;
	break;
	case 's':
		state->default_op = hq9x_unknown;
		state->on_error = ERROR_SIGNAL;
	break;
	case 'h':
		state->default_op = hq9x_unknown;
		state->on_error = ERROR_HALT;
	break;
	}
	switch(on_ws)
	{
	case 'i':
		state->ops[' '] = state->ops['\t'] = hq9x_nop;
	break;
	case 'u':
		state->ops[' '] = state->ops['\t'] = hq9x_unknown;
	break;
	}
	switch(on_nl)
	{
	case 'w':
		state->ops['\n'] = state->ops[' '];
	break;
	case 'i':
		state->ops['\n'] = hq9x_nop;
	break;
	case 'u':
		state->ops['\n'] = hq9x_unknown;
	break;
	}
	state->input.file = source;
	source_get_text(&state->input);
	state->input.file = NULL;
	if(version == HQ9X_BEFUNGE93)
	{
		source_ensure_grid(&state->input, 25, 80);
	}

	if(source != stdin)
		fclose(source);
	hq9x_interpret(state);
	source_free(&state->input);
	return state->exit_with_accumulator ? state->accumulator : 0;
}

