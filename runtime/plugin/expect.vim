let s:State = {}


function s:State.new(patterns)
  let this = copy(self)
  let this._ = {}
  let this._.patterns = patterns
  return this
endfunction


let Expect = {}
let Expect.LINE_BUFFER_MAX_LEN = 100
let Expect.State = s:State


function Expect.create(initial_state, target)
  let expect = copy(self)
  let expect._ = {}
  let expect._.line_buffer = []
  let expect._.stack = [a:initial_state]
  let expect._.target = a:target
  let expect._.target.on_stdout = function('s:JobOutput')
  let expect._.target.on_stderr = function('s:JobOutput')
  let expect._.target._expect = expect
  return expect._.target
endfunction


function Expect.push(state)
  call add(self._.stack, state)
endfunction


function Expect.pop()
  if len(self._.stack) == 1
    throw 'State stack cannot be empty'
  endif
  return remove(self._.stack, -1)
endfunction


function Expect.switch(state)
  let old_state = self._.stack[-1]
  self._.stack[-1] = state
  return old_state
endfunction


function Expect.feed(lines)
  if empty(a:lines)
    return
  endif
  let lines = a:lines
  let linebuf = self._.line_buffer
  if lines[0] != "\n" && !empty(linebuf)
    " continue the previous line
    let linebuf[-1] .= lines[0]
    call remove(lines, 0)
  endif
  " append the newly received lines to the line buffer
  let linebuf += lines
  " keep trying to match handlers while the line isnt empty
  while !empty(linebuf)
    let match_idx = self.match(self._.stack[-1], linebuf)
    if match_idx == -1
      break
    endif
    let linebuf = linebuf[match_idx + 1 : ]
  endwhile
  " shift excess lines from the buffer
  while len(linebuf) > self.LINE_BUFFER_MAX_LEN
    call remove(linebuf, 0)
  endwhile
  let self._.line_buffer = linebuf
endfunction


function Expect.match(state, lines)
  let lines = a:lines
  if empty(lines)
    return -1
  endif
  " search for a match using the list of patterns
  for [pattern, handler] in state._.patterns
    let matches = matchlist(lines, pattern)
    if empty(matches)
      continue
    endif
    let match_idx = match(lines, pattern)
    call call(state[handler], args, self._.target)
    return match_idx
  endfor
endfunction


function s:JobOutput(id, lines)
  self._expect.feed(lines)
endfunction

