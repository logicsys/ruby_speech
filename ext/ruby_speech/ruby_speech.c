#include "ruby.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pcre2_code_free_wrapper(void *code)
{
  pcre2_code_free((pcre2_code *)code);
}

static VALUE method_compile_regex(VALUE self, VALUE regex_string)
{
  int errorcode;
  PCRE2_SIZE erroroffset;
  uint32_t options = 0;
  const char *regex = StringValueCStr(regex_string);

  pcre2_code *compiled_regex = pcre2_compile(
    (PCRE2_SPTR)regex,
    PCRE2_ZERO_TERMINATED,
    options,
    &errorcode,
    &erroroffset,
    NULL
  );

  if (compiled_regex == NULL) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
    rb_raise(rb_eRuntimeError, "PCRE2 compilation failed at offset %d: %s", (int)erroroffset, buffer);
  }

  VALUE compiled_regex_wrapper = Data_Wrap_Struct(rb_cObject, 0, pcre2_code_free_wrapper, compiled_regex);
  rb_iv_set(self, "@regex", compiled_regex_wrapper);

  return Qnil;
}

#define MAX_INPUT_SIZE 128
#define OVECTOR_SIZE 30
#define WORKSPACE_SIZE 1024

/**
 * Check if no more digits can be added to input and match
 * @param compiled_regex the regex used in the initial match
 * @param input the input to check
 * @return true if end of match (no more input can be added)
 */
static int is_match_end(pcre2_code *compiled_regex, const char *input)
{
  pcre2_match_data *match_data;
  int input_size = (int)strlen(input);
  char search_input[MAX_INPUT_SIZE + 2];
  const char *search_set = "0123456789#*ABCD";
  const char *search = strchr(search_set, input[input_size - 1]); /* start with last digit in input */
  int i;

  /* For each digit in search_set, check if input + search_set digit is a potential match.
     If so, then this is not a match end.
   */
  if (strlen(input) > MAX_INPUT_SIZE) {
    return 0;
  }

  match_data = pcre2_match_data_create_from_pattern(compiled_regex, NULL);
  sprintf(search_input, "%sZ", input);

  for (i = 0; i < 16; i++) {
    int result;
    if (!*search) {
      search = search_set;
    }
    search_input[input_size] = *search++;
    result = pcre2_match(compiled_regex, (PCRE2_SPTR)search_input, input_size + 1, 0, 0, match_data, NULL);
    if (result > -1) {
      pcre2_match_data_free(match_data);
      return 0;
    }
  }
  pcre2_match_data_free(match_data);
  return 1;
}

static VALUE method_find_match(VALUE self, VALUE buffer)
{
  VALUE RubySpeech  = rb_const_get(rb_cObject, rb_intern("RubySpeech"));
  VALUE GRXML       = rb_const_get(RubySpeech, rb_intern("GRXML"));
  VALUE NoMatch     = rb_const_get(GRXML, rb_intern("NoMatch"));
  pcre2_code *compiled_regex;
  pcre2_match_data *match_data;
  pcre2_match_context *mcontext;
  int *workspace;
  int result = 0;
  char *input = StringValueCStr(buffer);

  Data_Get_Struct(rb_iv_get(self, "@regex"), pcre2_code, compiled_regex);

  if (!compiled_regex) {
    return rb_class_new_instance(0, NULL, NoMatch);
  }

  match_data = pcre2_match_data_create_from_pattern(compiled_regex, NULL);

  /* Try regular match first to check for complete match */
  result = pcre2_match(compiled_regex, (PCRE2_SPTR)input, strlen(input), 0, 0, match_data, NULL);

  if (result > 0) {
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (ovector && ovector[1] == strlen(input)) {
      /* Complete match - entire input was consumed */
      int is_end = is_match_end(compiled_regex, input);
      pcre2_match_data_free(match_data);
      if (is_end) {
        return rb_funcall(self, rb_intern("match_for_buffer"), 2, buffer, Qtrue);
      }
      return rb_funcall(self, rb_intern("match_for_buffer"), 1, buffer);
    }
  }

  /* Try partial match with DFA for partial matching behavior */
  pcre2_match_data_free(match_data);
  match_data = pcre2_match_data_create_from_pattern(compiled_regex, NULL);
  mcontext = pcre2_match_context_create(NULL);
  workspace = (int *)malloc(WORKSPACE_SIZE * sizeof(int));

  result = pcre2_dfa_match(compiled_regex, (PCRE2_SPTR)input, strlen(input), 0,
    PCRE2_PARTIAL_HARD, match_data, mcontext,
    workspace, WORKSPACE_SIZE);

  pcre2_match_context_free(mcontext);
  free(workspace);

  if (result > 0 || result == PCRE2_ERROR_PARTIAL || (int)strlen(input) == 0) {
    VALUE PotentialMatch = rb_const_get(GRXML, rb_intern("PotentialMatch"));
    pcre2_match_data_free(match_data);
    return rb_class_new_instance(0, NULL, PotentialMatch);
  }

  pcre2_match_data_free(match_data);
  return rb_class_new_instance(0, NULL, NoMatch);
}

void Init_ruby_speech(void)
{
  VALUE RubySpeech  = rb_define_module("RubySpeech");
  VALUE GRXML       = rb_define_module_under(RubySpeech, "GRXML");
  VALUE Matcher     = rb_define_class_under(GRXML, "Matcher", rb_cObject);

  rb_define_method(Matcher, "find_match", method_find_match, 1);
  rb_define_method(Matcher, "compile_regex", method_compile_regex, 1);
}
