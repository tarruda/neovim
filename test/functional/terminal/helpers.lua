local helpers = require('test.functional.helpers')
local Screen = require('test.functional.ui.screen')
local nvim_dir = helpers.nvim_dir
local execute, nvim = helpers.execute, helpers.nvim

local function feed_data(data)
  nvim('set_var', 'term_data', data)
  nvim('command', 'call jobsend(b:term_job_id, term_data)')
end

local function feed_termcode(data)
  -- feed with the job API
  nvim('command', 'call jobsend(b:term_job_id, "\\x1b'..data..'")')
end
-- some helpers for controlling the terminal. the codes were taken from
-- infocmp xterm-256color which is less what libvterm understands
-- civis/cnorm
local function hide_cursor() feed_termcode('[?25l') end
local function show_cursor() feed_termcode('[?25h') end
-- smcup/rmcup
local function enter_altscreen() feed_termcode('[?1049h') end
local function exit_altscreen() feed_termcode('[?1049l') end
-- smcup/rmcup


local function screen_setup(extra_height)
  if not extra_height then extra_height = 0 end
  local screen = Screen.new(50, 7 + extra_height)
  screen:set_default_attr_ids({
    [1] = {reverse = true},   -- focused cursor
    [2] = {background = 11},  -- unfocused cursor
  })
  screen:set_default_attr_ignore({
    [1] = {bold = true},
    [2] = {foreground = 12},
    [3] = {bold = true, reverse = true},
    [5] = {background = 11},
    [6] = {foreground = 130},
  })

  screen:attach(false)
  -- tty-test puts the terminal into raw mode and echoes all input. tests are
  -- done by feeding it with terminfo codes to control the display and
  -- verifying output with screen:expect.
  execute('term "' ..nvim_dir.. '/tty-test"')
  -- wait for "tty ready" to be printed before each test or the terminal may
  -- still be in canonical mode(will echo characters for example)
  --
  local empty_line =  '                                                   '
  local expected = {
    'tty ready                                          ',
    '{1: }                                                  ',
    empty_line,
    empty_line,
    empty_line,
    empty_line,
  }
  for i = 1, extra_height do
    table.insert(expected, empty_line)
  end

  table.insert(expected, '-- TERMINAL --                                     ')
  screen:expect(table.concat(expected, '\n'))
  return screen
end

return {
  feed_data = feed_data,
  feed_termcode = feed_termcode,
  hide_cursor = hide_cursor,
  show_cursor = show_cursor,
  enter_altscreen = enter_altscreen,
  exit_altscreen = exit_altscreen,
  screen_setup = screen_setup
}
