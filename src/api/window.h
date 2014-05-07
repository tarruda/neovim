#ifndef NEOVIM_API_WINDOW_H
#define NEOVIM_API_WINDOW_H

#include <stdint.h>
#include <stdbool.h>

#include "api/defs.h"

/// Gets the current buffer in a window
///
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return The buffer handle
Buffer window_get_buffer(Window window, Error *err);

/// Gets the cursor position in the window
///
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return the (row, col) tuple
Position window_get_cursor(Window window, Error *err);

/// Sets the cursor position in the window
///
/// @param window The window handle
/// @param pos the (row, col) tuple representing the new position
/// @param[out] err Details of an error that may have occurred
void window_set_cursor(Window window, Position pos, Error *err);

/// Gets the window height
///
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return the height in rows
uint64_t window_get_height(Window window, Error *err);

/// Sets the window height. This will only succeed if the screen is split
/// horizontally.
///
/// @param window The window handle
/// @param height the new height in rows
/// @param[out] err Details of an error that may have occurred
void window_set_height(Window window, uint64_t height, Error *err);

/// Gets the window width
///
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return the width in columns
uint64_t window_get_width(Window window, Error *err);

/// Gets a window variable
///
/// @param window The window handle
/// @param name The variable name
/// @param[out] err Details of an error that may have occurred
/// @return The variable value
Object window_get_var(Window window, String name, Error *err);

/// Sets a window variable
///
/// @param window The window handle
/// @param name The variable name
/// @param value The variable value
/// @param[out] err Details of an error that may have occurred
void window_set_var(Window window, String name, Object value, Error *err);

/// Gets a window option value
///
/// @param window The window handle
/// @param name The option name
/// @param[out] err Details of an error that may have occurred
/// @return The option value
String window_get_option(Window window, String name, Error *err);

/// Sets a window option value
///
/// @param window The window handle
/// @param name The option name
/// @param value The option value
/// @param[out] err Details of an error that may have occurred
void window_set_option(Window window, String name, String value, Error *err);

/// Gets the window position in display cells. First position is zero.
///
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return The (row, col) tuple with the window position
Position window_get_pos(Window window, Error *err);

/// Gets the window tab page
/// 
/// @param window The window handle
/// @param[out] err Details of an error that may have occurred
/// @return The tab page that contains the window
Tabpage window_get_tabpage(Window window, Error *err);

/// Checks if a window is valid
///
/// @param window The window handle
/// @return true if the window is valid, false otherwise
bool window_is_valid(Window window);

#endif // NEOVIM_API_WINDOW_H

