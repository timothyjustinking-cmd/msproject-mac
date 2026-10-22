@echo off
rem Launches Ghidra with its bundled JDK. Tools live locally (not in Dropbox) at C:\dev\tools.
rem In Ghidra: File > Open Project > ghidra-project\WINPROJ.gpr (next to this file).
set "JAVA_HOME=C:\dev\tools\jdk-21.0.12.1+1"
set "PATH=%JAVA_HOME%\bin;%PATH%"
start "" "C:\dev\tools\ghidra_12.1.4_PUBLIC\ghidraRun.bat"
