@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set ZIPDIR=roms\zips
set BIOSDIR=roms\neogeo
set PAGESIZE=20
set TMPALL=%TEMP%\ng_all.tmp
set TMPFILT=%TEMP%\ng_filtered.tmp

if not exist "%ZIPDIR%" (
    echo Pasta "%ZIPDIR%" nao encontrada. Baixe/organize as roms la primeiro.
    pause
    goto fim
)

:menu
cls
echo ===============================================
echo   zera330 - roms\zips (jogue direto do .zip)
echo ===============================================
echo   Digite parte do nome do jogo pra filtrar (ex: mslug)
echo   ENTER sem nada = lista todos os jogos
echo   "sair" pra encerrar
echo ===============================================
set /p FILTRO=Filtro:

if /i "%FILTRO%"=="sair" goto fim

dir /b /on "%ZIPDIR%\*.zip" > "%TMPALL%" 2>nul
if "%FILTRO%"=="" (
    copy /y "%TMPALL%" "%TMPFILT%" >nul
) else (
    findstr /i "%FILTRO%" "%TMPALL%" > "%TMPFILT%"
)

set /a TOTAL=0
for /f "usebackq delims=" %%L in ("%TMPFILT%") do set /a TOTAL+=1
if %TOTAL%==0 (
    echo.
    echo Nenhum jogo encontrado com "%FILTRO%".
    pause
    goto menu
)

set PAGE=0
goto showpage

:showpage
for /L %%X in (1,1,%PAGESIZE%) do set "ITEM%%X="

set /a START=PAGE*PAGESIZE+1
if %START% GTR %TOTAL% (
    echo Nao ha mais paginas.
    set /a PAGE-=1
    set /a START=PAGE*PAGESIZE+1
)
set /a ENDIDX=START+PAGESIZE-1

cls
echo ===============================================
if "%FILTRO%"=="" (
    echo   Todos os jogos  (%TOTAL% no total)
) else (
    echo   Filtro "%FILTRO%"  (%TOTAL% jogos)
)
set /a PAGENUM=PAGE+1
set /a PAGETOTAL=(TOTAL+PAGESIZE-1)/PAGESIZE
echo   Pagina %PAGENUM% de %PAGETOTAL%
echo ===============================================
set /a I=0
for /f "usebackq delims=" %%L in ("%TMPFILT%") do (
    set /a I+=1
    if !I! GEQ %START% if !I! LEQ %ENDIDX% (
        set /a REL=!I!-%START%+1
        set "ITEM!REL!=%%L"
        for %%F in ("%%L") do echo   !REL! - %%~nF
    )
)
echo ===============================================
echo   [N] proxima pagina   [P] pagina anterior   [F] novo filtro   [S] sair
set /p ESC=Numero do jogo ou comando:

if /i "%ESC%"=="S" goto fim
if /i "%ESC%"=="F" goto menu
if /i "%ESC%"=="N" (
    set /a PAGE+=1
    goto showpage
)
if /i "%ESC%"=="P" (
    if %PAGE% GTR 0 set /a PAGE-=1
    goto showpage
)

set "SELECTED=!ITEM%ESC%!"
if not defined SELECTED (
    echo Opcao invalida.
    pause
    goto showpage
)

set "ROMFILE=%ZIPDIR%\%SELECTED%"
for %%F in ("%SELECTED%") do set "GAMENAME=%%~nF"

echo.
echo Abrindo %GAMENAME% (direto do zip, sem extrair)...
echo Controles: setas/WASD = direcao, Z X C V = A B C D, ENTER = start, 5/Backspace = coin, F1 = service
echo.
build\bin\zera330c.exe "%CD%\%ROMFILE%" "%CD%\%BIOSDIR%"

pause
goto showpage

:fim
endlocal
