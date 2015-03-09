local helpers = require('test.functional.helpers')
local thelpers = require('test.functional.terminal.helpers')
local clear, eq, curbuf = helpers.clear, helpers.eq, helpers.curbuf
local feed, nvim = helpers.feed, helpers.nvim
local feed_data = thelpers.feed_data

describe('terminal', function()
  local screen

  before_each(function()
    clear()
    -- set the statusline to a constant value because of variables like pid
    -- and current directory and to improve visibility of splits
    nvim('set_option', 'statusline', '==========')
    screen = thelpers.screen_setup(3)
  end)

  after_each(function()
    screen:detach()
  end)

  describe('split horizontally', function()
    before_each(function()
      nvim('command', 'sp')
    end)

    local function reduce_height()
      screen:expect([[
        tty ready                                         |
        rows: 3, cols: 50                                 |
        ^                                                  |
        ~                                                 |
        ==========                                        |
        tty ready                                         |
        rows: 3, cols: 50                                 |
                                                          |
        ==========                                        |
        -- TERMINAL --                                    |
      ]])
    end

    it('uses the minimum height of all window displaying it', reduce_height)

    describe('and then vertically', function()
      before_each(function()
        reduce_height()
        nvim('command', 'vsp')
      end)

      local function reduce_width()
        screen:expect([[
          rows: 3, cols: 50        |rows: 3, cols: 50       |
          rows: 3, cols: 24        |rows: 3, cols: 24       |
          ^                         |                        |
          ~                        |~                       |
          ==========                ==========              |
          rows: 3, cols: 50                                 |
          rows: 3, cols: 24                                 |
                                                            |
          ==========                                        |
          -- TERMINAL --                                    |
        ]])
        feed('<c-\\><c-n>gg')
        screen:expect([[
          ^tty ready                |rows: 3, cols: 50       |
          rows: 3, cols: 50        |rows: 3, cols: 24       |
          rows: 3, cols: 24        |{1: }                       |
          {1: }                        |~                       |
          ==========                ==========              |
          rows: 3, cols: 50                                 |
          rows: 3, cols: 24                                 |
          {1: }                                                 |
          ==========                                        |
                                                            |
        ]])
      end

      it('uses the minimum width of all window displaying it', reduce_width)

      describe('and then closes one of the vertical splits with q:', function()
        before_each(function()
          reduce_width()
          nvim('command', 'q')
          feed('<c-w>ja')
        end)

        it('will restore the width', function()
          -- screen:expect([[
          --   rows: 3, cols: 24                                 |
          --   rows: 3, cols: 50                                 |
          --                                                     |
          --   ~                                                 |
          --   ==========                                        |
          --   rows: 3, cols: 24                                 |
          --   rows: 3, cols: 50                                 |
          --   ^                                                  |
          --   ==========                                        |
          --   -- TERMINAL --                                    |
          -- ]])
        end)
      end)
    end)
  end)
end)
