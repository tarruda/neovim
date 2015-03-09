local helpers = require('test.functional.helpers')
local thelpers = require('test.functional.terminal.helpers')
local feed, clear, nvim = helpers.feed, helpers.clear, helpers.nvim
local hide_cursor = thelpers.hide_cursor
local show_cursor = thelpers.show_cursor


describe('cursor', function()
  local screen

  before_each(function()
    clear()
    screen = thelpers.screen_setup()
  end)

  it('is followed by the screen cursor', function()
    feed('hello')
    screen:expect([[
      tty ready                                         |
      hello{1: }                                            |
                                                        |
                                                        |
                                                        |
                                                        |
      -- TERMINAL --                                    |
    ]])
  end)

  it('is highlighted when not focused', function()
    feed('<c-\\><c-n>')
    screen:expect([[
      tty ready                                         |
      {2: }                                                 |
                                                        |
                                                        |
                                                        |
      ^                                                  |
                                                        |
    ]])
  end)

  describe('with number column', function()
    before_each(function()
      feed('<c-\\><c-n>:set number<cr>')
    end)

    it('is positioned correctly when unfocused', function()
      screen:expect([[
          1 tty ready                                     |
          2 {2: }                                             |
          3                                               |
          4                                               |
          5                                               |
          6 ^                                              |
        :set number                                       |
      ]])
    end)

    it('is positioned correctly when focused', function()
      feed('i')
      screen:expect([[
          1 tty ready                                     |
          2 {1: }                                             |
          3                                               |
          4                                               |
          5                                               |
          6                                               |
        -- TERMINAL --                                    |
      ]])
    end)
  end)

  describe('when invisible', function()
    it('is not highlighted and is detached from screen cursor', function()
      hide_cursor()
      screen:expect([[
        tty ready                                         |
                                                          |
                                                          |
                                                          |
                                                          |
                                                          |
        -- TERMINAL --                                    |
      ]])
      show_cursor()
      screen:expect([[
        tty ready                                         |
        {1: }                                                 |
                                                          |
                                                          |
                                                          |
                                                          |
        -- TERMINAL --                                    |
      ]])
      -- same for when the terminal is unfocused
      feed('<c-\\><c-n>')
      hide_cursor()
      screen:expect([[
        tty ready                                         |
                                                          |
                                                          |
                                                          |
                                                          |
        ^                                                  |
                                                          |
      ]])
      show_cursor()
      screen:expect([[
        tty ready                                         |
        {2: }                                                 |
                                                          |
                                                          |
                                                          |
        ^                                                  |
                                                          |
      ]])
    end)
  end)
end)
