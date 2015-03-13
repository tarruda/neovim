sign define GdbBreakpoint text=●
sign define GdbCurrentLine text=⇒

let s:gdb_port = 7778
let s:match_buffer_maxlen = 50
let s:run_nvim = "gdbserver localhost:%d build/bin/nvim"
let s:run_tests = "GDB=1 GDBSERVER_PORT=%d make test"
let s:run_gdb = "gdb -q -f build/bin/nvim"
let s:breakpoints = {}


function! s:GdbOutput()
  if !exists('t:gdbclient_id')
    return
  endif
  if v:job_data[1] == 'exit'
    return
  endif
  if empty(v:job_data[2])
    return
  endif
  if v:job_data[2][0] != "\n" && !empty(t:match_buffer)
    " continue the previous line
    let t:match_buffer[-1] .= v:job_data[2][0]
    call remove(v:job_data[2], 0)
  endif
  call extend(t:match_buffer, v:job_data[2])
  for [pattern, Handler] in t:handlers
    let matches = matchlist(t:match_buffer, pattern)
    if !empty(matches)
      let match_idx = match(t:match_buffer, pattern)
      let t:match_buffer = t:match_buffer[match_idx + 1 : ]
      call call(Handler, matches[1:])
      if !exists('t:gdbclient_id')
        return
      endif
    endif
  endfor
  while len(t:match_buffer) > s:match_buffer_maxlen
    call remove(t:match_buffer, 0)
  endwhile
endfunction


function! s:GdbSpawn(gdbserver_cmd, gdbclient_cmd, gdb_port)
  " Create a new tab
  tabnew
  let t:gdb_port = a:gdb_port
  let t:match_buffer = []
  " create splits
  sp
  " save window number that will be displaying the current file
  let t:jump_window = 1
  if type(a:gdbserver_cmd) == type('')
    vsp
    " spawn gdbserver 
    enew | let t:gdbserver_id = termopen(printf(a:gdbserver_cmd, t:gdb_port))
    let t:jump_window = 2
    let t:gdbserver_buf = bufnr('%')
  endif
  " go to the bottom window and spawn gdb client
  wincmd j
  enew | let t:gdbclient_id = termopen(a:gdbclient_cmd)
  let t:gdbclient_buf = bufnr('%')
  let gdbclient_pattern = 'term://*//'.string(b:term_job_pid).':*'
  let autocmd = 'au JobActivity '.gdbclient_pattern.'* nested call s:GdbOutput()'
  augroup NvimDebugGroup
    au!
    exe autocmd
  augroup END
  tnoremap <silent> <f8> <c-\><c-n>:GdbContinue<cr>i
  tnoremap <silent> <f10> <c-\><c-n>:GdbNext<cr>i
  tnoremap <silent> <f11> <c-\><c-n>:GdbStep<cr>i
  tnoremap <silent> <f12> <c-\><c-n>:GdbFinish<cr>i
  " Breakpoint 1, normal_cmd (oap=0x7fffffffd9e0, toplevel=true) at /home/tarruda/pub-dev/neovim/src/nvim/normal.c:450
  let t:handlers = [
        \ ['\v\[Inferior\ +.{-}\ +exited\ +normally', function('s:Exited')],
        \ ['Continuing.', function('s:Running')],
        \ ['\v[\o32]{2}([^:]+):(\d+):\d+', function('s:Jump')],
        \ ['(gdb)', function('s:Paused')]
        \ ]
  exe t:jump_window 'wincmd w'
endfunction


function! s:GdbTest(...)
  let cmd = s:run_tests
  if a:0
    let cmd = 'TEST_SCREEN_TIMEOUT=1000000 TEST_FILTER="'.a:1.'" '.cmd
  endif
  call s:GdbSpawn(cmd, s:run_gdb, s:gdb_port)
  let t:reattach_on_exit = 1
endfunction


function! s:GdbKill()
  if !exists('t:gdbclient_id')
    throw 'Not debugging'
  endif
  tunmap <f8>
  tunmap <f10>
  tunmap <f11>
  tunmap <f12>
  unlet t:gdbclient_id
  if exists('t:current_buf')
    exe 'sign unplace 4999 buffer='.t:current_buf
  endif
  exe 'bd! '.t:gdbclient_buf
  if exists('t:gdbserver_buf')
    exe 'bd! '.t:gdbserver_buf
  endif
  tabclose
endfunction


function! s:GdbToggleBreak()
  let file_name = bufname('%')
  let file_breakpoints = get(s:breakpoints, file_name, {})
  let linenr = line('.')
  if has_key(file_breakpoints, linenr)
    call remove(file_breakpoints, linenr)
  else
    let file_breakpoints[linenr] = 1
  endif
  let s:breakpoints[file_name] = file_breakpoints
  call s:RefreshSigns()
  call s:RefreshBreakpoints()
endfunction


function! s:GdbClearBreak()
  let s:breakpoints = {}
  call s:RefreshSigns()
  call s:RefreshBreakpoints()
endfunction


function! s:GdbSend(data)
  if !exists('t:gdbclient_id')
    throw "Can't use this command now"
  endif
  call jobsend(t:gdbclient_id, a:data."\<cr>")
endfunction


function! s:Attach()
  call s:GdbSend(printf('target remote localhost:%d', t:gdb_port))
endfunction


function! s:Paused(...)
  let t:state = 'paused'
  if !exists('t:initialized')
    call s:GdbSend('set confirm off')
    call s:GdbSend('set pagination off')
    call s:GdbSend('set remotetimeout 50')
    call s:Attach()
    call s:RefreshBreakpoints()
    let t:initialized = 1
    GdbContinue
  endif
endfunction


function! s:Running(...)
  let t:state = 'running'
endfunction


function! s:Jump(file, line, ...)
  let window = winnr()
  exe t:jump_window 'wincmd w'
  let t:current_buf = bufnr('%')
  let target_buf = bufnr(a:file, 1)
  if bufnr('%') != target_buf
    exe 'buffer ' target_buf
    let t:current_buf = target_buf
  endif
  exe ':' a:line
  let t:current_line = a:line
  exe window 'wincmd w'
  call s:RefreshSigns()
endfunction


function! s:Exited(...)
  if exists('t:reattach_on_exit')
    " Refresh to force a delete of all watchpoints
    call s:RefreshBreakpoints()
    sleep 1
    call s:Attach()
    call s:GdbSend('continue')
  else
    call s:GdbKill()
  endif
endfunction


function! s:RefreshSigns()
  let buf = bufnr('%')
  exe 'sign unplace * buffer='.buf
  if exists('t:current_line') && exists('t:current_buf')
    exe 'sign unplace 4999 buffer='.t:current_buf
    exe 'sign place 4999 name=GdbCurrentLine line='.t:current_line.' buffer='.t:current_buf
  endif
  let id = 5000
  for linenr in keys(get(s:breakpoints, bufname('%'), {}))
    exe 'sign place '.id.' name=GdbBreakpoint line='.linenr.' buffer='.buf
    let id = id + 1
  endfor
endfunction


function! s:RefreshBreakpoints()
  if !exists('t:gdbclient_id')
    return
  endif
  if t:state == 'running'
    " pause first
    call jobsend(t:gdbclient_id, "\<c-c>")
  endif
  if exists('t:has_breakpoints') && t:has_breakpoints
    call s:GdbSend('delete')
  endif
  let t:has_breakpoints = 0
  for [file, breakpoints] in items(s:breakpoints)
    for linenr in keys(breakpoints)
      let t:has_breakpoints = 1
      call s:GdbSend('break '.file.':'.linenr)
    endfor
  endfor
endfunction


function! s:GetExpression(...) range
  if a:firstline == 1 && a:lastline == line('$')
    return expand('<cword>')
  endif
  let [lnum1, col1] = getpos("'<")[1:2]
  let [lnum2, col2] = getpos("'>")[1:2]
  let lines = getline(lnum1, lnum2)
  let lines[-1] = lines[-1][:col2 - 1]
  let lines[0] = lines[0][col1 - 1:]
  return join(lines, "\n")
endfunction


function! s:Eval(expr)
  call s:GdbSend(printf('print %s', a:expr))
endfunction


function! s:Watch(expr)
  let expr = a:expr
  if expr[0] != '&'
    let expr = '&' . expr
  endif

  call s:Eval(expr)
  call s:GdbSend('watch *$')
endfunction


command! GdbDebugNvim call s:GdbSpawn(s:run_nvim, s:run_gdb, s:gdb_port)
command! -nargs=1 GdbDebugServer call s:GdbSpawn(0, s:run_gdb, <f-args>)
command! -nargs=? GdbDebugTest call s:GdbTest(<f-args>)
command! GdbDebugStop call s:GdbKill()
command! GdbToggleBreakpoint call s:GdbToggleBreak()
command! GdbClearBreakpoints call s:GdbClearBreak()
command! GdbContinue call s:GdbSend("c")
command! GdbNext call s:GdbSend("n")
command! GdbStep call s:GdbSend("s")
command! GdbFinish call s:GdbSend("finish")
command! GdbFrameUp call s:GdbSend("up")
command! GdbFrameDown call s:GdbSend("down")
command! GdbInterrupt call jobsend(t:gdbclient_id, "\<c-c>info line\<cr>")
command! -range GdbEval call s:Eval(s:GetExpression(<f-args>))
command! -range GdbWatch call s:Watch(s:GetExpression(<f-args>))


nnoremap <silent> <f8> :GdbContinue<cr>
nnoremap <silent> <f10> :GdbNext<cr>
nnoremap <silent> <f11> :GdbStep<cr>
nnoremap <silent> <f12> :GdbFinish<cr>
nnoremap <silent> <c-b> :GdbToggleBreakpoint<cr>
nnoremap <silent> <m-pageup> :GdbFrameUp<cr>
nnoremap <silent> <m-pagedown> :GdbFrameDown<cr>
nnoremap <silent> <f9> :GdbEval<cr>
vnoremap <silent> <f9> :GdbEval<cr>
nnoremap <silent> <m-f9> :GdbWatch<cr>
vnoremap <silent> <m-f9> :GdbWatch<cr>
