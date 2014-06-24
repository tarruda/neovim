#include <stdint.h>
#include <stdbool.h>

#include "nvim/os/script_host.h"
#include "nvim/api/vim.h"
#include "nvim/api/private/helpers.h"
#include "nvim/os/channel.h"
#include "nvim/os/shell.h"
#include "nvim/os/os.h"
#include "nvim/vim.h"
#include "nvim/path.h"
#include "nvim/message.h"
#include "nvim/ex_getln.h"
#include "nvim/ex_cmds_defs.h"

typedef enum {
  kScriptHostTypePython = 0
} ScriptHostType;

static struct {
  char *name, *command, **argv;
  uint64_t channel_id;
} hosts[] = {
  [kScriptHostTypePython] = {
    .name = "python",
    .command = "python -c \"import neovim; neovim.run_script_host()\"",
    .argv = NULL,
    .channel_id = 0
  }
};

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/script_host.c.generated.h"
#endif

bool script_host_has(char *name)
{
  for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
    if (!STRICMP(name, hosts[i].name)) {
      return host_exists(i);
    }
  }

  return false;
}

void script_host_ex_python(exarg_T *eap)
{
  execute(kScriptHostTypePython, eap);
}

void script_host_ex_pyfile(exarg_T *eap)
{
  execute_file(kScriptHostTypePython, eap);
}

void script_host_ex_pydo(exarg_T *eap)
{
  range_do(kScriptHostTypePython, eap);
}

void script_host_f_pyeval(typval_T *argvars, typval_T *rettv)
{
  Object rv = eval(kScriptHostTypePython, vim_to_object(argvars));
  Error err = {.set = false};
  object_to_vim(rv, rettv, &err);

  if (err.set) {
    EMSG("Error converting value back to vim");
  }
}

static void execute(ScriptHostType type, exarg_T *eap)
{
  if (!validate_script_host(type)) {
    return;
  }

  uint64_t channel_id = get_script_host_id(type);

  if (!channel_id) {
    return;
  }

  char *script = (char *)script_get(eap, eap->arg);

  if (!eap->skip) {
    Object result;
    bool errored;
    channel_send_call(channel_id,
                      "execute",
                      STRING_OBJ(cstr_to_string(
                          script ? script : (char *)eap->arg)),
                      &result,
                      &errored);
    if (errored) {
      report_error(result);
    }
  }

  free(script);
}

static void execute_file(ScriptHostType type, exarg_T *eap)
{
  if (!validate_script_host(type)) {
    return;
  }

  uint64_t channel_id = get_script_host_id(type);

  if (!channel_id) {
    return;
  }

  char buffer[MAXPATHL];
  vim_FullName(eap->arg, (uint8_t *)buffer, sizeof(buffer), false);

  Object result;
  bool errored;
  channel_send_call(channel_id,
                    "execute_file",
                    STRING_OBJ(cstr_to_string(buffer)),
                    &result,
                    &errored);

  if (errored) {
    report_error(result);
  }
}

static void range_do(ScriptHostType type, exarg_T *eap)
{
  if (!validate_script_host(type)) {
    return;
  }

  uint64_t channel_id = get_script_host_id(type);

  if (!channel_id) {
    return;
  }

  Array arg = {0, 0, 0};
  ADD(arg, INTEGER_OBJ(eap->line1));
  ADD(arg, INTEGER_OBJ(eap->line2));
  ADD(arg, STRING_OBJ(cstr_to_string((char *)eap->arg)));

  Object result;
  bool errored;
  channel_send_call(channel_id,
                    "range_do",
                    ARRAY_OBJ(arg),
                    &result,
                    &errored);

  if (errored) {
    report_error(result);
  }
}

static Object eval(ScriptHostType type, Object script)
{
  if (!validate_script_host(type)) {
    return NIL;
  }

  uint64_t channel_id = get_script_host_id(type);

  if (!channel_id) {
    return NIL;
  }

  Object result;
  bool errored;
  channel_send_call(channel_id, "eval", script, &result, &errored); 

  if (errored) {
    report_error(result);
    return NIL;
  }

  return result;
}

static uint64_t get_script_host_id(ScriptHostType type)
{
  if (hosts[type].channel_id && !channel_exists(hosts[type].channel_id)) {
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "A host for %s exited prematurely and had to be restarted",
             hosts[type].name);
    EMSG(buf);
    // Channel was closed prematurely, reset state
    hosts[type].argv = NULL;
    hosts[type].channel_id = 0;
  }

  if (!hosts[type].channel_id) {
    if (!can_execute(type)) {
      return 0;
    }

    // Initialize the host
    hosts[type].channel_id = channel_from_job(hosts[type].argv);
  }

  return hosts[type].channel_id;
}

static bool validate_script_host(ScriptHostType type)
{
  if (!host_exists(kScriptHostTypePython)) {
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "A host for %s is not available",
             hosts[type].name);
    EMSG(buf);
    return false; 
  }

  return true;
}

static bool host_exists(ScriptHostType type)
{
  return hosts[type].channel_id || can_execute(type);
}

static bool can_execute(ScriptHostType type)
{
  if (!hosts[type].argv) {
    hosts[type].argv = shell_build_argv((uint8_t *)hosts[type].command, NULL);
  }

  return os_can_exe((uint8_t *)hosts[type].argv[0]);
}

static void report_error(Object result)
{
  vim_err_write(result.data.string);
  vim_err_write((String) {.data = "\n", .size = 1});
}
