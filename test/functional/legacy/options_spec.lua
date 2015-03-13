before_first = function(cb) before_each(once(cb)) end
-- Test if ":options" throws any exception. The options window seems to mess
-- other tests, so restart nvim in the teardown hook

local helpers = require('test.functional.helpers')
local command, clear = helpers.command, helpers.clear

describe('options', function()
  before_first(clear)

  it('is working', function()
    command('options')
  end)
end)
