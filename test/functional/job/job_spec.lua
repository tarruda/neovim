
local helpers = require('test.functional.helpers')
local clear, nvim, eq, neq, ok, expect, eval, next_msg, run, stop, session
  = helpers.clear, helpers.nvim, helpers.eq, helpers.neq, helpers.ok,
  helpers.expect, helpers.eval, helpers.next_message, helpers.run,
  helpers.stop, helpers.session
local nvim_dir, insert = helpers.nvim_dir, helpers.insert
local source = helpers.source


describe('jobs', function()
  local channel

  before_each(function()
    clear()
    channel = nvim('get_api_info')[1]
    nvim('set_var', 'channel', channel)
    source([[
    function! s:OnEvent(id, data, user, event)
      call rpcnotify(g:channel, a:event, a:user, a:data)
    endfunction
    let g:job_opts = {
    \ 'on_stdout': function('s:OnEvent'),
    \ 'on_stderr': function('s:OnEvent'),
    \ 'on_exit': function('s:OnEvent')
    \ }
    ]])
  end)

  it('returns 0 when it fails to start', function()
    local status, rv = pcall(eval, "jobstart([])")
    eq(false, status)
    ok(rv ~= nil)
  end)

  it('invokes callbacks when the job writes and exits', function()
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'stdout', {0, {'', ''}}}, next_msg())
    eq({'notification', 'exit', {0, 0}}, next_msg())
  end)

  it('allows interactive commands', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    neq(0, eval('j'))
    nvim('command', 'call jobsend(j, "abc\\n")')
    eq({'notification', 'stdout', {0, {'abc', ''}}}, next_msg())
    nvim('command', 'call jobsend(j, "123\\nxyz\\n")')
    eq({'notification', 'stdout', {0, {'123', 'xyz', ''}}}, next_msg())
    nvim('command', 'call jobsend(j, [123, "xyz", ""])')
    eq({'notification', 'stdout', {0, {'123', 'xyz', ''}}}, next_msg())
    nvim('command', "call jobstop(j)")
    eq({'notification', 'exit', {0, 0}}, next_msg())
  end)

  it('preserves NULs', function()
    -- Make a file with NULs in it.
    local filename = os.tmpname()
    local file = io.open(filename, "w")
    file:write("abc\0def\n")
    file:close()

    -- v:job_data preserves NULs.
    nvim('command', "let j = jobstart(['cat', '"..filename.."'], g:job_opts)")
    eq({'notification', 'stdout', {0, {'abc\ndef', ''}}}, next_msg())
    eq({'notification', 'exit', {0, 0}}, next_msg())
    os.remove(filename)

    -- jobsend() preserves NULs.
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    nvim('command', [[call jobsend(j, ["123\n456",""])]])
    eq({'notification', 'stdout', {0, {'123\n456', ''}}}, next_msg())
    nvim('command', "call jobstop(j)")
  end)

  it('will not buffer data if it doesnt end in newlines', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    nvim('command', 'call jobsend(j, "abc\\nxyz")')
    eq({'notification', 'stdout', {0, {'abc', 'xyz'}}}, next_msg())
    nvim('command', "call jobstop(j)")
    eq({'notification', 'exit', {0, 0}}, next_msg())
  end)

  it('can preserve newlines', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    nvim('command', 'call jobsend(j, "a\\n\\nc\\n\\n\\n\\nb\\n\\n")')
    eq({'notification', 'stdout',
      {0, {'a', '', 'c', '', '', '', 'b', '', ''}}}, next_msg())
  end)

  it('can preserve nuls', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    nvim('command', 'call jobsend(j, ["\n123\n", "abc\\nxyz\n", ""])')
    eq({'notification', 'stdout', {0, {'\n123\n', 'abc\nxyz\n', ''}}},
      next_msg())
    nvim('command', "call jobstop(j)")
    eq({'notification', 'exit', {0, 0}}, next_msg())
  end)

  it('can avoid sending final newline', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    nvim('command', 'call jobsend(j, ["some data", "without\nfinal nl"])')
    eq({'notification', 'stdout', {0, {'some data', 'without\nfinal nl'}}},
      next_msg())
    nvim('command', "call jobstop(j)")
    eq({'notification', 'exit', {0, 0}}, next_msg())
  end)

  it('will not allow jobsend/stop on a non-existent job', function()
    eq(false, pcall(eval, "jobsend(-1, 'lol')"))
    eq(false, pcall(eval, "jobstop(-1)"))
  end)

  it('will not allow jobstop twice on the same job', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    neq(0, eval('j'))
    eq(true, pcall(eval, "jobstop(j)"))
    eq(false, pcall(eval, "jobstop(j)"))
  end)

  it('will not cause a memory leak if we leave a job running', function()
    nvim('command', "call jobstart(['cat', '-'], g:job_opts)")
  end)

  it('can pass numbers as user data', function()
    nvim('command', 'let g:job_opts.user = 5')
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'stdout', {5, {'', ''}}}, next_msg())
    eq({'notification', 'exit', {5, 0}}, next_msg())
  end)

  it('can pass strings as user data', function()
    nvim('command', 'let g:job_opts.user = "str"')
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'stdout', {'str', {'', ''}}}, next_msg())
    eq({'notification', 'exit', {'str', 0}}, next_msg())
  end)

  it('can pass references as user data', function()
    nvim('command', 'let g:job_opts.user = {"some": [0, "user data"]}')
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'stdout', {{some = {0, 'user data'}}, {'', ''}}},
      next_msg())
    eq({'notification', 'exit', {{some = {0, 'user data'}}, 0}}, next_msg())
  end)

  it('can omit data callbacks', function()
    nvim('command', 'unlet g:job_opts.on_stdout')
    nvim('command', 'unlet g:job_opts.on_stderr')
    nvim('command', 'let g:job_opts.user = 5')
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'exit', {5, 0}}, next_msg())
  end)

  it('can omit exit callback', function()
    nvim('command', 'unlet g:job_opts.on_exit')
    nvim('command', 'let g:job_opts.user = 5')
    nvim('command', "call jobstart(['echo'], g:job_opts)")
    eq({'notification', 'stdout', {5, {'', ''}}}, next_msg())
  end)

  it('will pass return code with the exit event', function()
    nvim('command', 'let g:job_opts.user = 5')
    nvim('command', "call jobstart([&sh, '-c', 'return 55'], g:job_opts)")
    eq({'notification', 'exit', {5, 55}}, next_msg())
  end)

  -- FIXME need to wait until jobsend succeeds before calling jobstop
  pending('will only emit the "exit" event after "stdout" and "stderr"', function()
    nvim('command', "let j = jobstart(['cat', '-'], g:job_opts)")
    local jobid = nvim('eval', 'j')
    nvim('eval', 'jobsend(j, "abcdef")')
    nvim('eval', 'jobstop(j)')
    eq({'notification', 'j', {0, {jobid, 'stdout', {'abcdef'}}}}, next_msg())
    eq({'notification', 'j', {0, {jobid, 'exit'}}}, next_msg())
  end)

  describe('running tty-test program', function()
    local function next_chunk()
      local rv = ''
      while true do
        local msg = next_msg()
        local data = msg[3][2]
        for i = 1, #data do
          data[i] = data[i]:gsub('\n', '\000')
        end
        rv = table.concat(data, '\n')
        rv = rv:gsub('\r\n$', '')
        if rv ~= '' then
          break
        end
      end
      return rv
    end

    local function send(str)
      nvim('command', 'call jobsend(j, "'..str..'")')
    end

    before_each(function() 
      -- the full path to tty-test seems to be required when running on travis.
      insert(nvim_dir .. '/tty-test')
      nvim('command', 'let g:job_opts.pty = 1')
      nvim('command', 'let exec = [expand("<cfile>:p")]')
      nvim('command', "let j = jobstart(exec, g:job_opts)")
      eq('tty ready', next_chunk())
    end)

    it('echoing input', function()
      send('test')
      eq('test', next_chunk())
    end)

    it('resizing window', function()
      nvim('command', 'call jobresize(j, 40, 10)')
      eq('rows: 10, cols: 40', next_chunk())
      nvim('command', 'call jobresize(j, 10, 40)')
      eq('rows: 40, cols: 10', next_chunk())
    end)
  end)
end)
