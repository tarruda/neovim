-- tty-test puts the terminal into raw mode and echoes all input. tests are
-- done by feeding it with terminfo codes to control the display and verifying
-- output with screen:expect.
local helpers = require('test.functional.helpers')
local Screen = require('test.functional.ui.screen')
local clear, feed, nvim_dir = helpers.clear, helpers.feed, helpers.nvim_dir
local execute, nvim, eq = helpers.execute, helpers.nvim, helpers.eq
local curbuf = helpers.curbuf

describe('terminal', function()
  local screen

  local function feed_data(data)
    nvim('set_var', 'term_data', data)
    nvim('command', 'call jobsend(job_id, term_data)')
  end

  local function feed_termcode(data)
    -- feed with the job API
    nvim('command', 'call jobsend(job_id, "\\x1b'..data..'")')
  end
  -- some helpers for controlling the terminal. the codes were taken from
  -- infocmp xterm-256color which is less what libvterm understands
  local function hide_cursor() feed_termcode('[?25l') end
  local function show_cursor() feed_termcode('[?25h') end


  before_each(function()
    clear()
    screen = Screen.new(35, 7)
    screen:set_default_attr_ids({
      [1] = {bold = true},
      [2] = {background = 16574799}  -- unfocused cursor color
    })
    screen:attach()
    execute('let job_id = termopen("' ..nvim_dir.. '/tty-test") | startinsert')
    -- wait for "tty ready" to be printed before each test or the terminal may
    -- still be in canonical mode(will echo characters for example)
    screen:expect([[
      tty ready                          |
      ^                                  |
                                         |
                                         |
                                         |
                                         |
      {1:-- TERMINAL --}                     |
    ]])
  end)

  after_each(function()
    screen:detach()
  end)

  describe('scrollback', function()
    before_each(function()
      -- terminal buffers always have a minimum of lines equal to the window
      -- height
      eq(6, curbuf('line_count'))
      feed_data({'line1', 'line2', 'line3', 'line4', ''})
      screen:expect([[
        tty ready                          |
        line1                              |
        line2                              |
        line3                              |
        line4                              |
        ^                                  |
        {1:-- TERMINAL --}                     |
      ]])
      eq(6, curbuf('line_count'))
    end)

    describe('when a linefeed is output at the end of the screen', function()
      it('is increased', function()
        feed_data({'line5', ''})
        screen:expect([[
          line1                              |
          line2                              |
          line3                              |
          line4                              |
          line5                              |
          ^                                  |
          {1:-- TERMINAL --}                     |
        ]])
        eq(7, curbuf('line_count'))
        feed_data({'line6', 'line7', 'line8'})
        screen:expect([[
          line3                              |
          line4                              |
          line5                              |
          line6                              |
          line7                              |
          line8^                             |
          {1:-- TERMINAL --}                     |
        ]])
        feed('<c-\\><c-n>6k')
        screen:expect([[
          ^ine2                              |
          line3                              |
          line4                              |
          line5                              |
          line6                              |
          line7                              |
                                             |
        ]])
        feed('gg')
        screen:expect([[
          ^ty ready                          |
          line1                              |
          line2                              |
          line3                              |
          line4                              |
          line5                              |
                                             |
        ]])
        feed('G')
        screen:expect([[
          line3                              |
          line4                              |
          line5                              |
          line6                              |
          line7                              |
          ^ine8{2: }                             |
                                             |
        ]])
      end)
    end)
  end)

  describe('cursor', function()
    it('is followed by the screen cursor', function()
      feed('hello')
      screen:expect([[
        tty ready                          |
        hello^                             |
                                           |
                                           |
                                           |
                                           |
        {1:-- TERMINAL --}                     |
      ]])
    end)

    it('is highlighted when not focused', function()
      feed('<c-\\><c-n>')
      screen:expect([[
        tty ready                          |
        {2: }                                  |
                                           |
                                           |
                                           |
        ^                                  |
                                           |
      ]])
    end)

    describe('when invisible', function()
      it('is not highlighted and is detached from screen cursor', function()
        hide_cursor()
        screen:expect([[
          tty ready                          |
                                             |
                                             |
                                             |
                                             |
          ^                                  |
          {1:-- TERMINAL --}                     |
        ]])
        show_cursor()
        screen:expect([[
          tty ready                          |
          ^                                  |
                                             |
                                             |
                                             |
                                             |
          {1:-- TERMINAL --}                     |
        ]])
        -- same for when the terminal is unfocused
        feed('<c-\\><c-n>')
        hide_cursor()
        screen:expect([[
          tty ready                          |
                                             |
                                             |
                                             |
                                             |
          ^                                  |
                                             |
        ]])
        show_cursor()
        screen:expect([[
          tty ready                          |
          {2: }                                  |
                                             |
                                             |
                                             |
          ^                                  |
                                             |
        ]])
      end)
    end)

  end)
end)
