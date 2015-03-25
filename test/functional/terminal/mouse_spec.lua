local Screen = require('test.functional.ui.screen')
local helpers = require('test.functional.helpers')
local thelpers = require('test.functional.terminal.helpers')
local clear, eq, curbuf = helpers.clear, helpers.eq, helpers.curbuf
local feed, execute, nvim = helpers.feed, helpers.execute, helpers.nvim
local feed_data = thelpers.feed_data

describe('terminal mouse', function()
  local screen

  before_each(function()
    clear()
    nvim('set_option', 'statusline', '==========')
    nvim('command', 'highlight StatusLine cterm=NONE')
    nvim('command', 'highlight StatusLineNC cterm=NONE')
    nvim('command', 'highlight VertSplit cterm=NONE')
    screen = thelpers.screen_setup()
    local lines = {}
    for i = 1, 30 do
      table.insert(lines, 'line'..tostring(i))
    end
    table.insert(lines, '')
    feed_data(lines)
    screen:expect([[
      line26                                            |
      line27                                            |
      line28                                            |
      line29                                            |
      line30                                            |
      {1: }                                                 |
      -- TERMINAL --                                    |
    ]])
  end)

  after_each(function()
    screen:detach()
  end)

  describe('when the terminal has focus', function()
    it('will exit focus when scrolled', function()
      feed('<MouseDown><0,0>')
      screen:expect([[
        line23                                            |
        line24                                            |
        line25                                            |
        line26                                            |
        line27                                            |
        ^line28                                            |
                                                          |
      ]])
    end)

    describe('with mouse events enabled by the program', function()
      before_each(function()
        thelpers.enable_mouse()
        thelpers.feed_data('mouse enabled\n')
        screen:expect([[
          line27                                            |
          line28                                            |
          line29                                            |
          line30                                            |
          mouse enabled                                     |
          {1: }                                                 |
          -- TERMINAL --                                    |
        ]])
      end)

      it('will forward mouse clicks to the program', function()
        feed('<LeftMouse><1,2>')
        screen:expect([[
          line27                                            |
          line28                                            |
          line29                                            |
          line30                                            |
          mouse enabled                                     |
           "#{1: }                                              |
          -- TERMINAL --                                    |
        ]])
      end)

      it('will forward mouse scroll to the program', function()
        feed('<MouseDown><0,0>')
        screen:expect([[
          line27                                            |
          line28                                            |
          line29                                            |
          line30                                            |
          mouse enabled                                     |
          `!!{1: }                                              |
          -- TERMINAL --                                    |
        ]])
      end)
    end)

    describe('with a split window and other buffer', function()
      before_each(function()
        -- mouse enabled doesn't interfere when interacting with other
        -- windows
        thelpers.enable_mouse()
        feed('<c-\\><c-n>:vsp | enew | set number<cr>30iline\n<esc>')
        feed('<c-w>li')
        screen:expect([[
           27 line                 |line22                  |
           28 line                 |line23                  |
           29 line                 |line24                  |
           30 line                 |rows: 5, cols: 24       |
           31                      |{1: }                       |
          ==========                ==========              |
          -- TERMINAL --                                    |
        ]])
      end)

      it('wont lose focus if another window is scrolled', function()
        feed('<MouseDown><0,0><MouseDown><0,0>')
        screen:expect([[
           21 line                 |line22                  |
           22 line                 |line23                  |
           23 line                 |line24                  |
           24 line                 |rows: 5, cols: 24       |
           25 line                 |{1: }                       |
          ==========                ==========              |
          -- TERMINAL --                                    |
        ]])
        feed('<S-MouseUp><0,0>')
        screen:expect([[
           26 line                 |line22                  |
           27 line                 |line23                  |
           28 line                 |line24                  |
           29 line                 |rows: 5, cols: 24       |
           30 line                 |{1: }                       |
          ==========                ==========              |
          -- TERMINAL --                                    |
        ]])
      end)

      it('will lose focus if another window is clicked', function()
        feed('<LeftMouse><5,1>')
        screen:expect([[
           27 line                 |line22                  |
           28 l^ine                 |line23                  |
           29 line                 |line24                  |
           30 line                 |rows: 5, cols: 24       |
           31                      |{2: }                       |
          ==========                ==========              |
                                                            |
        ]])
      end)
    end)
  end)
end)
