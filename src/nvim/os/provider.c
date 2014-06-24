#include <stdint.h>
#include <stdbool.h>

#include "nvim/os/provider.h"
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

static struct {
  char *name, *command, **argv;
  uint64_t channel_id;
} providers[] = {
  [kPythonProvider] = {
    .name = "python",
    .command = "python -c \"import neovim; neovim.run_script_host()\"",
    .argv = NULL,
    .channel_id = 0
  }
};

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/provider.c.generated.h"
#endif

bool provider_has(char *name)
{
  for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); i++) {
    if (!STRICMP(name, providers[i].name)) {
      return provider_exists(i);
    }
  }

  return false;
}

void provider_execute(Provider provider, exarg_T *eap)
{
  if (!validate_provider(provider)) {
    return;
  }

  uint64_t channel_id = get_provider_id(provider);

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

void provider_execute_file(Provider provider, exarg_T *eap)
{
  if (!validate_provider(provider)) {
    return;
  }

  uint64_t channel_id = get_provider_id(provider);

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

void provider_do_range(Provider provider, exarg_T *eap)
{
  if (!validate_provider(provider)) {
    return;
  }

  uint64_t channel_id = get_provider_id(provider);

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
                    "do_range",
                    ARRAY_OBJ(arg),
                    &result,
                    &errored);

  if (errored) {
    report_error(result);
  }
}

void provider_eval(Provider provider, typval_T *argvars, typval_T *rettv)
{
  if (!validate_provider(provider)) {
    return;
  }

  uint64_t channel_id = get_provider_id(provider);

  if (!channel_id) {
    return;
  }

  Object result;
  bool error;
  channel_send_call(channel_id,
                    "eval",
                    vim_to_object(argvars),
                    &result,
                    &error); 

  if (error) {
    report_error(result);
    return;
  }

  Error err = {.set = false};
  object_to_vim(result, rettv, &err);

  if (err.set) {
    EMSG("Error converting value back to vim");
  }
}

static uint64_t get_provider_id(Provider provider)
{
  if (providers[provider].channel_id
      && !channel_exists(providers[provider].channel_id)) {
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "A provider for %s exited prematurely and had to be restarted",
             providers[provider].name);
    EMSG(buf);
    // Channel was closed prematurely, reset state
    providers[provider].argv = NULL;
    providers[provider].channel_id = 0;
  }

  if (!providers[provider].channel_id) {
    if (!can_execute(provider)) {
      return 0;
    }

    // Initialize the provider
    providers[provider].channel_id =
      channel_from_job(providers[provider].argv);
  }

  return providers[provider].channel_id;
}

static bool validate_provider(Provider provider)
{
  if (!provider_exists(provider)) {
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "A provider for %s is not available",
             providers[provider].name);
    EMSG(buf);
    return false; 
  }

  return true;
}

static bool provider_exists(Provider provider)
{
  return providers[provider].channel_id || can_execute(provider);
}

static bool can_execute(Provider provider)
{
  if (!providers[provider].argv) {
    providers[provider].argv =
      shell_build_argv((uint8_t *)providers[provider].command, NULL);
  }

  return os_can_exe((uint8_t *)providers[provider].argv[0]);
}

static void report_error(Object result)
{
  vim_err_write(result.data.string);
  vim_err_write((String) {.data = "\n", .size = 1});
}
