#ifndef NEOVIM_API_H
#define NEOVIM_API_H

void api_push_keys(char *str);
void api_command(char *str);
void api_eval(char *str);
void api_bind_eval(char *str);
void api_list_runtime_paths(char *str);
char ** api_list_buffers(void);
char ** api_list_windows(void);
char ** api_list_tabpages(void);
char * api_get_current_line(void);
uint32_t api_get_current_buffer(void);
uint32_t api_get_current_window(void);
uint32_t api_get_current_tabpage(void);
void api_set_current_line(char *line);
void api_set_current_buffer(uint32_t id);
void api_set_current_window(uint32_t id);
void api_set_current_tabpage(uint32_t id);
char * api_get_option(char *name);
void api_set_option(char *name, char *value);
void api_out_write(char *str);
void api_err_write(char *str);

#endif // NEOVIM_API_H
