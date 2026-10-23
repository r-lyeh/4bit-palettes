@echo off
start https://zxart.ee/pictures/top
echo Save html page in this folder
pause
move "Top - ZX-Art_files\*." .
ren "id=*" idd*.png
rd /q /s "Top - ZX-Art_files"
del *.html
