#ifndef NVIM_OS_PROVIDER_H
#define NVIM_OS_PROVIDER_H

#include "nvim/vim.h"
#include "nvim/ex_cmds_defs.h"

typedef enum {
  kPythonProvider = 0
} Provider;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/provider.h.generated.h"
#endif

#endif  // NVIM_OS_PROVIDER_H
