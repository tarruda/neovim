sign define GdbBreakpoint text=●
sign define GdbCurrentLine text=⇒


let s:gdb_port = 7778
let s:run_nvim = "gdbserver localhost:%d build/bin/nvim"
let s:run_tests = "GDB=1 GDBSERVER_PORT=%d make test"
let s:run_gdb = "gdb -q -f build/bin/nvim"
let s:breakpoints = {}


let s:GdbPaused = Expect.State.new([
      \ ['Continuing.', 'continue'],
      \ ['\v[\o32]{2}([^:]+):(\d+):\d+', 'jump'],
      \ ])


function s:GdbPaused.continue(...)
  let self._.state = 'running'
  call self._expect.switch(s:GdbRunning)
endfunction


function s:GdbPaused.jump(file, line, ...)
  if tabpagenr() != self._.tab
    " Don't jump if we are not in the debugger tab
    return
  endif
  let window = winnr()
  exe self._.jump_window 'wincmd w'
  let self._.current_buf = bufnr('%')
  let target_buf = bufnr(a:file, 1)
  if bufnr('%') != target_buf
    exe 'buffer ' target_buf
    let self._.current_buf = self._.target_buf
  endif
  exe ':' a:line
  let self._.current_line = a:line
  exe window 'wincmd w'
  call s:RefreshSigns()
endfunction


let s:GdbRunning = Expect.State.new([
      \ ['(gdb)', 'pause'],
      \ ['\v\[Inferior\ +.{-}\ +exited\ +normally', 'exit'],
      \ ])


function s:GdbRunning.pause(...)
  self._.state = 'paused'
  call self._expect.switch(s:GdbPaused)
  if self._.initialized
    call self.send('set confirm off')
    call self.send('set pagination off')
    call self.send('set remotetimeout 50')
    call self.attach()
    call s:RefreshBreakpoints()
    let self._.initialized = 1
    self._.continue()
  endif
endfunction


function s:GdbRunning.exit(...)
  if self._.reattach
    " Refresh to force a delete of all watchpoints
    call s:RefreshBreakpoints()
    sleep 1
    call self.attach()
    call self.send('continue')
  else
    call self.kill()
  endif
endfunction


let s:Gdb = {}


function s:Gdb.spawn(server_cmd, client_cmd, port, reattach)
  let this = Expect.create(s:GdbPaused, copy(self))
  let this._ = {}
  " gdbserver port
  let this._.port = a:port
  let this._.reattach = a:reattach
  " window number that will be displaying the current file
  let this._.jump_window = 1
  let this._.current_buf = -1
  " Create new tab for the debugging view
  tabnew
  let this._.tab = tabpagenr()
  " create horizontal split to display the current file and maybe gdbserver
  sp
  let this._.server_buf = -1
  " Create new tab for the debugging view
  if type(a:server_cmd) == type('')
    " spawn gdbserver in a vertical split
    vsp
    enew | let this._.server_id = termopen(printf(a:server_cmd, a:port))
    let this._.jump_window = 2
    let this._.server_buf = bufnr('%')
  endif
  " go to the bottom window and spawn gdb client
  wincmd j
  let this._.client_id = termopen(a:client_cmd, this)
  let this._.client_buf = bufnr('%')
  tnoremap <silent> <f8> <c-\><c-n>:GdbContinue<cr>i
  tnoremap <silent> <f10> <c-\><c-n>:GdbNext<cr>i
  tnoremap <silent> <f11> <c-\><c-n>:GdbStep<cr>i
  tnoremap <silent> <f12> <c-\><c-n>:GdbFinish<cr>i
  " go to the window that displays the current file
  exe this._.jump_window 'wincmd w'
  return this
endfunction


function s:Gdb.test(...)
  if exists('g:gdb')
    throw 'Gdb already running'
  endif
  let cmd = s:run_tests
  if a:0
    let cmd = 'TEST_SCREEN_TIMEOUT=1000000 TEST_FILTER="'.a:1.'" '.cmd
  endif
  let g:gdb = self.spawn(cmd, s:run_gdb, s:gdb_port, 1)
endfunction


function s:Gdb.kill()
  tunmap <f8>
  tunmap <f10>
  tunmap <f11>
  tunmap <f12>
  if self._.current_buf != -1
    exe 'sign unplace 4999 buffer='.self._.current_buf
  endif
  exe 'bd! '.self._.client_buf
  if self._.server_buf != -1
    exe 'bd! '.self._.server_buf
  endif
  exe 'tabclose '.self._.tab
  unlet g:gdb
endfunction


function! s:Gdb.send(data)
  call jobsend(self._.client_id, a:data."\<cr>")
endfunction


function! s:Gdb.attach()
  call self.send(printf('target remote localhost:%d', self._.port))
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


function! s:RefreshSigns()
  let buf = bufnr('%')
  exe 'sign unplace * buffer='.buf
  if exists('g:gdb') && g:gdb._.current_line && g:gdb._.current_buf
    exe 'sign unplace 4999 buffer='.g:gdb._.current_buf
    exe 'sign place 4999 name=GdbCurrentLine line='.g:gdb._.current_line.' buffer='.g:gdb._.current_buf
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
