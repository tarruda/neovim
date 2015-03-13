Script for debugging Neovim with gdb and the embedded terminal emulator.

After sourcing it, the following commands are available:

- GdbDebugStart: launch a gdb debugging session
- GdbDebugStop: stop the current debugging session
- GdbToggleBreakpoint: add/remove a breakpoint in the line under the cursor
- GdbClearBreakpoints: remove all breakpoints
- GdbContinue: continue after pausing
- GdbNext: step over
- GdbStep: step into
- GdbFinish: step out
- GdbFrameUp: show the upper frame
- GdbFrameDown: show the lower frame

These mappings make it more easy to use some of the above commands:

- <f8>: GdbContinue
- <f10>: GdbNext
- <f11>: GdbStep
- <f12>: GdbFinish
- <c-b>: GdbToggleBreakpoint
- <a-pageup>: GdbFrameUp
- <a-pagedown>: GdbFrameDown
