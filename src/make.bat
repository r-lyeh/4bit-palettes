rd /q /s output & cl zx_pal.c /O2 /Oy /MT /DNDEBUG && zx_pal & del *.obj *.exe

if exist \prj\tools\nconvert.exe (
\prj\tools\nconvert.exe -out png -o "compressed\%%" "..\*.png"
copy /y compressed\*.png ..
rd /q /s compressed
)
