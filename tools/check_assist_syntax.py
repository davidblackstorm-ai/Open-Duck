"""Read existing MSVC flags; syntax-check one owned staging AI source without E writes."""
from pathlib import Path
import re, subprocess, sys
native=Path(__file__).resolve().parents[1]
work=native.parent
source=(native/sys.argv[1]).resolve()
assert source.is_relative_to(native) and source.is_file()
command=(work/'PluginEditor-syntax.rsp').read_text(encoding='utf-8')
command=re.sub(r'\s+"[^"]+\.cpp"\s*$', '', command)
command+=' /I"'+str(native/'Source')+'" "'+str(source)+'"\n'
response=work/(source.stem+'-assist-syntax.rsp')
response.write_text(command,encoding='utf-8')
launcher=work/(source.stem+'-assist-syntax.cmd')
launcher.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl.exe @"'+str(response)+'"\n',encoding='ascii')
raise SystemExit(subprocess.call(['cmd.exe','/d','/c',str(launcher)],cwd=work))
