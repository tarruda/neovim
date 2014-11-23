local helpers = require('test.functional.helpers')
local clear, nvim = helpers.clear, helpers.nvim


local function ui_clear()
  helpers.clear()
  nvim('subscribe', 'ui_update')
end

local function screen_wait()
end

return {
  ui_clear = ui_clear
}

