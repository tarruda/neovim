local helpers = require('test.functional.helpers')
local thelpers = require('test.functional.terminal.helpers')
local feed, clear, nvim = helpers.feed, helpers.clear, helpers.nvim
local wait, execute, eq = helpers.wait, helpers.execute, helpers.eq


describe('terminal buffer', function()
  local screen

  before_each(function()
    clear()
    execute('set modifiable swapfile undolevels=20')
    wait()
    screen = thelpers.screen_setup()
  end)

  describe('when a new file is edited', function()
    before_each(function()
      feed('<c-\\><c-n>:set bufhidden=wipe<cr>:enew<cr>')
      screen:expect([[
        ^                                                  |
        ~                                                 |
        ~                                                 |
        ~                                                 |
        ~                                                 |
        ~                                                 |
        :enew                                             |
      ]])
    end)

    it('will hide the buffer, ignoring the bufhidden option', function()
      feed(':bnext:l<esc>')
      screen:expect([[
        ^                                                  |
        ~                                                 |
        ~                                                 |
        ~                                                 |
        ~                                                 |
        ~                                                 |
                                                          |
      ]])
    end)
  end)

  describe('swap and undo', function()
    before_each(function()
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

    it('does not create swap files', function()
      local swapfile = nvim('command_output', 'swapname'):gsub('\n', '')
      eq(nil, io.open(swapfile))
    end)

    it('does not create undofiles files', function()
      local undofile = nvim('eval', 'undofile(bufname("%"))')
      eq(nil, io.open(undofile))
    end)
  end)

  it('cannot be modified directly', function()
    feed('<c-\\><c-n>dd')
    screen:expect([[
      tty ready                                         |
      {2: }                                                 |
                                                        |
                                                        |
                                                        |
      ^                                                  |
      E21: Cannot make changes, 'modifiable' is off     |
    ]])
  end)

  it('sends data to the terminal when the "put" operator is used', function()
    feed('<c-\\><c-n>gg"ayG')
    screen:expect([[
      ^tty ready                                         |
      {2: }                                                 |
                                                        |
                                                        |
                                                        |
                                                        |
      6 lines yanked                                    |
    ]])
    execute('let @a = "appended " . @a')
    feed('"ap')
    screen:expect([[
      ^tty ready                                         |
      appended tty ready                                |
                                                        |
                                                        |
                                                        |
                                                        |
      :let @a = "appended " . @a                        |
    ]])
  end)

  it('can be deleted', function()
    feed('<c-\\><c-n>:bd!<cr>')
    screen:expect([[
      ^                                                  |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      :bd!                                              |
    ]])
    execute('bnext')
    screen:expect([[
      ^                                                  |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      ~                                                 |
      :bnext                                            |
    ]])
  end)
end)

