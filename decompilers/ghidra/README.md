1. Enable debugger in ghidra.
2. Replace ./Ghidra/Debug/Debugger-agent-gdb/data/debugger-launchers/raw-gdb.sh  with this script. 
3. Make sure the correct project is loaded otherwise Ghidra won't be able to map addresses and GDB will throw absolutely horrible error messages.
4. If everything worked right, enjoy debugging with source (well decompiled source).
