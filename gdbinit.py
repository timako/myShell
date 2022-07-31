gdb.execute('layout src')
gdb.execute('set follow-fork-mode child')
gdb.execute('break main')
gdb.execute('run')