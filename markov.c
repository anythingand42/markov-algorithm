#include <stdio.h>
#include <stdlib.h>

#define DELIM " -> "
#define MAX_STATE_LEN 128
#define MAX_OPERAND_LEN 16
#define MAX_RULE_STR_LEN (2 * MAX_OPERAND_LEN + (int)sizeof(DELIM) - 1)

enum MarkovResult { Matched = 0, MatchedAndTerminated = 1, Terminated = 2 };

int str_cmp(const char* a, const char* b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

int str_find(const char* str, const char* substr)
{
    int i, j;
    for (i = 0; str[i]; i++) {
        if (str[i] == substr[0]) {
            for (j = 0; substr[j] && str[i + j]; j++) {
                if (str[i + j] != substr[j])
                    goto try_next;
            }
            return i;
        }
    try_next:;
    }
    return -1;
}

void handle_args(int argc, char** argv, char** prules, char** pstate)
{
    int i;
    *prules = *pstate = NULL;
    for (i = 1; i < argc; i++) {
        if (str_cmp(argv[i], "--rules")) {
            if (i + 1 == argc) {
                fprintf(stderr, "Wrong arguments\n");
            }
            *prules = argv[i + 1];
        }
        if (str_cmp(argv[i], "--state")) {
            if (i + 1 == argc) {
                fprintf(stderr, "Wrong arguments\n");
            }
            *pstate = argv[i + 1];
        }
    }
    if (!*prules) {
        fprintf(stderr, "No rules defined\n");
        exit(1);
    }
    if (!*pstate) {
        fprintf(stderr, "Initial state is not defined\n");
        exit(1);
    }
    for (i = 0; (*pstate)[i]; i++) {
        if (i == MAX_STATE_LEN) {
            fprintf(stderr, "Initial state exceeded max length (%d)\n",
                MAX_STATE_LEN);
            exit(1);
        }
    }
}

typedef struct node {
    char left[MAX_OPERAND_LEN + 1];
    char right[MAX_OPERAND_LEN + 1];
    int end;
    struct node* next;
} MarkovRule;

/*
 * "1 -> 0" left: "1", right: "0"
 * "00 -> 1." left: "00", right: "1."
 */
void parse_rule_str(const char* rule_str, MarkovRule* rule)
{
    int left_end;
    left_end = str_find(rule_str, DELIM);
    if (left_end <= 0) {
        fprintf(stderr, "Syntax error: \"%s\"\n", rule_str);
        exit(1);
    }
    if (left_end > MAX_OPERAND_LEN) {
        fprintf(stderr, "Left operand exceeded max length (%d): \"%s\"\n",
            MAX_OPERAND_LEN, rule_str);
        exit(1);
    }
    int i, j;
    for (i = 0; i < left_end; i++) {
        if (rule_str[i] == '.') {
            fprintf(stderr,
                "Unexpected '.' character in left operand: \"%s\"\n", rule_str);
            exit(1);
        }
        rule->left[i] = rule_str[i];
    }
    rule->left[i] = '\0';
    rule->end = 0;
    for (i = left_end + sizeof(DELIM) - 1, j = 0; rule_str[i]; i++, j++) {
        if (j == MAX_OPERAND_LEN) {
            fprintf(stderr, "Right operand exceeded max length (%d): \"%s\"\n",
                MAX_OPERAND_LEN, rule_str);
            exit(1);
        }
        if (rule_str[i] == '.') {
            rule->end = 1;
            break;
        }
        rule->right[j] = rule_str[i];
    }
    rule->right[j] = '\0';
}

void add_rule(MarkovRule** phead, MarkovRule** ptail, const char* rule_str)
{
    MarkovRule* rule;

    rule = malloc(sizeof(MarkovRule));
    rule->next = NULL;

    parse_rule_str(rule_str, rule);

    if (!(*phead)) {
        *phead = *ptail = rule;
    } else {
        (*ptail)->next = rule;
        *ptail = rule;
    }
}

void print_rules(MarkovRule* cur)
{
    while (cur) {
        printf(
            "left: %s; right: %s; end: %d\n", cur->left, cur->right, cur->end);
        cur = cur->next;
    }
}

void free_rules_list(MarkovRule* head)
{
    MarkovRule* tmp;
    while (head) {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

void parse_rules_file(
    const char* file_name, MarkovRule** prules_head, MarkovRule** prules_tail)
{
    FILE* f;
    f = fopen(file_name, "r");
    if (!f) {
        perror(file_name);
        exit(1);
    }
    char buf[MAX_RULE_STR_LEN + 1];
    while (fgets(buf, sizeof(buf), f)) {
        int i;
        int str_end = -1;
        for (i = 0; i < sizeof(buf) && buf[i]; i++) {
            if (buf[i] == '\n') {
                str_end = i;
                break;
            }
        }
        if (str_end == -1) {
            fprintf(stderr, "Line exceeded max length (%d): \"%s\"\n",
                MAX_RULE_STR_LEN, buf);
            exit(1);
        }
        if (str_end) {
            buf[str_end] = '\0';
            add_rule(prules_head, prules_tail, buf);
        }
    }
    fclose(f);
}

/*
 * returns Matched if the rule applied
 * returns MatchedAndTerminated if the rule just applied was a terminating one
 * returns Terminated if no rule applied
 */
int markov_step(const MarkovRule* rule, const char state[MAX_STATE_LEN + 1],
    char result[MAX_STATE_LEN + 1])
{
    int match_start, i, j;
    while (rule) {
        match_start = str_find(state, rule->left);
        if (match_start >= 0) {
            for (i = 0; i < match_start; i++)
                result[i] = state[i];
            for (j = 0; state[i] == rule->left[j]; i++, j++) { }
            for (; state[i]; i++)
                result[i - j] = state[i];
            result[i] = '\0';
            return Matched;
        }
    }
    return Terminated;
}

int main(int argc, char** argv)
{
    char *rules_file_name, *state;

    handle_args(argc, argv, &rules_file_name, &state);
    printf("rules: %s, state: %s\n", rules_file_name, state);

    MarkovRule *rules_head, *rules_tail;
    rules_head = rules_tail = NULL;
    parse_rules_file(rules_file_name, &rules_head, &rules_tail);

    print_rules(rules_head);
    free_rules_list(rules_head);
    return 0;
}
