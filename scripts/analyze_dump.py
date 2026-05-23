import sys
from minidump.minidumpfile import MinidumpFile

dmp = sys.argv[1]
mdf = MinidumpFile.parse(dmp)

ei = mdf.exception
if ei:
    rec = ei.exception_records[0]
    er = rec.ExceptionRecord
    print(f'ExceptionCode:    {hex(er.ExceptionCode_raw)}  ({er.ExceptionCode})')
    print(f'ExceptionAddress: {hex(er.ExceptionAddress)}')
    print(f'ExceptionFlags:   {hex(er.ExceptionFlags)}')
    if er.ExceptionInformation:
        print(f'ExceptionInfo:    {[hex(x) for x in er.ExceptionInformation]}')
    print(f'CrashThreadId:    {rec.ThreadId}')
else:
    print('No exception record')

print()
print('=== Threads ===')
threads = mdf.threads.threads
print(f'Count: {len(threads)}')
for t in threads[:3]:
    print(f'  ThreadId: {t.ThreadId}')

print()
print('=== Modules (first 15) ===')
for m in list(mdf.modules.modules)[:15]:
    print(f'  {hex(m.baseaddress)}  {m.name}')
