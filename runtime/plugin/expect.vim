if exists("s:loaded_expect_vim")
  finish
endif
let s:loaded_expect_vim = 1

let s:State = {}

function s:State.new(patterns)
  let this = copy(self)
  let this._patterns = a:patterns
  return this
endfunction


let Expect = {}
let Expect.LINE_BUFFER_MAX_LEN = 100
let Expect.State = s:State


function Expect.create(initial_state, target)
  let expect = copy(self)
  let expect._line_buffer = []
  let expect._stack = [a:initial_state]
  let expect._target = a:target
  let expect._target.on_stdout = function('s:JobOutput')
  let expect._target.on_stderr = function('s:JobOutput')
  let expect._target._expect = expect
  return expect._target
endfunction


function Expect.push(state)
  call add(self._stack, a:state)
endfunction


function Expect.pop()
  if len(self._stack) == 1
    throw 'State stack cannot be empty'
  endif
  return remove(self._stack, -1)
endfunction


function Expect.switch(state)
  let old_state = self._stack[-1]
  let self._stack[-1] = a:state
  return old_state
endfunction


function Expect.feed(lines)
  if empty(a:lines)
    return
  endif
  let lines = a:lines
  let linebuf = self._line_buffer
  if lines[0] != "\n" && !empty(linebuf)
    " continue the previous line
    let linebuf[-1] .= lines[0]
    call remove(lines, 0)
  endif
  " append the newly received lines to the line buffer
  let linebuf += lines
  " keep trying to match handlers while the line isnt empty
  while !empty(linebuf)
    let match_idx = self.match(self._stack[-1], linebuf)
    if match_idx == -1
      break
    endif
    let linebuf = linebuf[match_idx + 1 : ]
  endwhile
  " shift excess lines from the buffer
  while len(linebuf) > self.LINE_BUFFER_MAX_LEN
    call remove(linebuf, 0)
  endwhile
  let self._line_buffer = linebuf
endfunction


function Expect.match(state, lines)
  let lines = a:lines
  if empty(lines)
    return -1
  endif
  " search for a match using the list of patterns
  for [pattern, handler] in a:state._patterns
    let matches = matchlist(lines, pattern)
    if empty(matches)
      continue
    endif
    let match_idx = match(lines, pattern)
    call call(a:state[handler], matches[1:], self._target)
    return match_idx
  endfor
endfunction


function! s:JobOutput(id, lines)
  call self._expect.feed(a:lines)
endfunction

